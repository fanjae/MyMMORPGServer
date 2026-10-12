#include "Map.h"
#include "Player.h"
#include "../ServerCore/Session.h"
#include "../ServerCore/Packet.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstddef>

namespace
{
    bool CanStand(const World3DDefinition& world, const MovementAction3D& probe)
    {
        // 발 위치와 지지면 검증에 더해 몬스터 몸체가 지형 내부로 들어가는지 확인한다.
        if (!world.ValidateAction(probe)) return false;
        const auto radius = static_cast<int64_t>(world.settings.radius) * 1000;
        const auto height = static_cast<int64_t>(world.settings.height) * 1000;
        for (const auto& box : world.boxes)
            if (probe.x + radius > static_cast<int64_t>(box.minX) * 1000 && probe.x - radius < static_cast<int64_t>(box.maxX) * 1000 &&
                probe.z + radius > static_cast<int64_t>(box.minZ) * 1000 && probe.z - radius < static_cast<int64_t>(box.maxZ) * 1000 &&
                probe.y < static_cast<int64_t>(box.maxY) * 1000 && probe.y + height > static_cast<int64_t>(box.minY) * 1000) return false;
        return true;
    }
}

void Map::SpawnFieldMonsters(uint16_t count)
{
    if (!Is3D() || !_fieldMonsters.empty() || count > MAX_FIELD_MONSTERS) return;
    // 샘플의 넓은 바닥에 세 마리를 배치하고 지형과 겹치는 시작 위치는 제외한다.
    _monsterPacketTick = UINT64_MAX;
    _fieldMonsters.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        double x = i < 3 ? -160.0 + i * 25 : -320.0 + (i % 12) * 48;
        double z = i < 3 ? -100.0 + i * 55 : -300.0 + (i / 12) * 48;
        MovementAction3D probe;
        probe.x = static_cast<int64_t>(x * 1000); probe.z = static_cast<int64_t>(z * 1000);
        probe.supportId = 1;
        if (CanStand(*_world3D, probe)) _fieldMonsters.push_back({i + 1, static_cast<uint8_t>(i % 3), x, 0, z, x - 35, x + 35});
    }
}
bool Map::SendFieldMonsters(Player& recipient)
{
    if (!Is3D() || recipient.GetMap() != this || !recipient.GetSession()) return false;
    if (_monsterPacketTick != _tick)
    {
        FieldMonstersHeader payload{GetMapId(), 0, _tick, static_cast<uint16_t>(_fieldMonsters.size())};
        PacketHeader header{static_cast<uint16_t>(sizeof(PacketHeader) + sizeof(payload) + payload.count * sizeof(FieldMonsterRecord)),
            static_cast<uint16_t>(GamePacketOpcode::FieldMonsters3D)};
        _monsterPacket.resize(header.size);
        memcpy(_monsterPacket.data(), &header, sizeof(header)); memcpy(_monsterPacket.data() + sizeof(header), &payload, sizeof(payload));
        for (size_t i = 0; i < _fieldMonsters.size(); ++i)
        {
            const auto& monster = _fieldMonsters[i];
            FieldMonsterRecord record{monster.id, monster.kind, static_cast<int32_t>(std::llround(monster.x * 1000)),
                static_cast<int32_t>(std::llround(monster.y * 1000)), static_cast<int32_t>(std::llround(monster.z * 1000)),
                static_cast<uint16_t>(monster.direction > 0 ? 9000 : 27000), monster.hp, 60, monster.revision};
            memcpy(_monsterPacket.data() + sizeof(header) + sizeof(payload) + i * sizeof(record), &record, sizeof(record));
        }
        _monsterPacketTick = _tick; ++_monsterMetrics.builds;
    }
    // Session::Send가 내부 버퍼로 복사하므로 다음 수신자에 같은 메모리를 재사용할 수 있다.
    uint64_t generation = recipient.GetGeneration();
    memcpy(_monsterPacket.data() + sizeof(PacketHeader) + offsetof(FieldMonstersHeader, generation), &generation, sizeof(generation));
    if (!recipient.GetSession()->Send(_monsterPacket.data(), static_cast<int32_t>(_monsterPacket.size())))
    { ++_monsterMetrics.failures; return false; }
    ++_monsterMetrics.packets; _monsterMetrics.bytes += _monsterPacket.size(); return true;
}
void Map::TickFieldMonsters(uint64_t, std::chrono::steady_clock::time_point now)
{
    // 아무도 없는 필드는 순찰 계산을 쉬고 재입장 시 큰 시간 차이를 누적하지 않는다.
    if (_fieldMonsters.empty() || _players.empty()) { _monsterUpdated = now; return; }
    // 서버 처리가 지연되어도 한 번에 이동할 시간을 제한해 순찰 위치가 크게 뛰지 않도록 한다.
    double delta = _monsterUpdated == std::chrono::steady_clock::time_point{} ? 0 :
        std::clamp(std::chrono::duration<double>(now - _monsterUpdated).count(), 0.0, 0.1);
    _monsterUpdated = now;
    _monsterMetrics.simulated += _fieldMonsters.size();
    for (auto& monster : _fieldMonsters)
    {
        if (!monster.hp)
        {
            if (now >= monster.respawnAt) { monster.hp = 60; ++monster.revision; monster.rest = 0.8; _monsterPacketTick = UINT64_MAX; }
            continue;
        }
        if (monster.rest > 0) { monster.rest -= delta; continue; }
        double next = monster.x + monster.direction * (18.0 + monster.kind * 3) * delta;
        MovementAction3D probe; probe.x = static_cast<int64_t>(next * 1000); probe.y = static_cast<int64_t>(monster.y * 1000);
        probe.z = static_cast<int64_t>(monster.z * 1000); probe.supportId = 1;
        // 순찰 끝이나 지형에 막히면 잠시 정지한 뒤 반대 방향으로 이동한다.
        if (next < monster.left || next > monster.right || !CanStand(*_world3D, probe))
        { monster.direction = -monster.direction; monster.rest = 0.8; }
        else monster.x = next;
    }
    // 서버에서 계산한 상태를 200ms마다 같은 필드의 모든 캐릭터에게 전달한다.
    if (now < _monsterBroadcast) return;
    _monsterBroadcast = now + std::chrono::milliseconds(200);
    for (const auto& entry : _players)
        if (entry.second->GetSession() && !SendFieldMonsters(*entry.second)) entry.second->GetSession()->RequestClose();
}

namespace
{
    bool ClearAttackLine(const World3DDefinition& world, double px, double py, double pz, double mx, double my, double mz)
    {
        // 공격 선분이 벽이나 단차 내부를 통과하면 피해를 적용하지 않는다.
        for (const auto& box : world.boxes)
        {
            double low = 0, high = 1;
            const double origin[]{px, py, pz}, delta[]{mx - px, my - py, mz - pz};
            const double min[]{double(box.minX), double(box.minY), double(box.minZ)}, max[]{double(box.maxX), double(box.maxY), double(box.maxZ)};
            bool intersects = true;
            for (int axis = 0; axis < 3; ++axis)
            {
                if (std::abs(delta[axis]) < 0.000001) { if (origin[axis] <= min[axis] || origin[axis] >= max[axis]) { intersects = false; break; } }
                else
                {
                    double a = (min[axis] - origin[axis]) / delta[axis], b = (max[axis] - origin[axis]) / delta[axis];
                    if (a > b) std::swap(a, b); low = (std::max)(low, a); high = (std::min)(high, b);
                    if (low >= high) { intersects = false; break; }
                }
            }
            if (intersects && high > 0 && low < 1) return false;
        }
        return true;
    }
}
bool Map::AttackField(Player& player, const BasicAttackRequest& request, std::chrono::steady_clock::time_point now)
{
    if (!Is3D() || player.GetMap() != this || player.GetSession() == nullptr) return false;
    BasicAttackEvent event{GetMapId(), player.GetGeneration(), request.sequence, player.GetCharacterId()};
    const auto& state = player.GetState3D();
    event.yaw = state.yaw;
    if (request.mapId != GetMapId() || request.generation != player.GetGeneration() ||
        request.actionSequence != player.GetActionSequence()) event.result = BasicAttackResult::InvalidState;
    else event.result = player.BeginAttack(request.sequence, now);
    bool swing = event.result == BasicAttackResult::Miss;
    if (swing)
    {
        // 서버가 확인한 발 위치·방향으로 앞쪽의 가장 가까운 살아 있는 몬스터 한 마리만 선택한다.
        double x = state.x / 1000.0, y = state.y / 1000.0, z = state.z / 1000.0;
        double yaw = state.yaw / 100.0 * 3.141592653589793 / 180, best = 60 * 60;
        FieldMonster* target = nullptr;
        for (auto& monster : _fieldMonsters)
        {
            double dx = monster.x - x, dz = monster.z - z, distance = dx * dx + dz * dz;
            if (!monster.hp || std::abs(monster.y - y) > 30 || distance > best ||
                (distance > 0.000001 && (dx * std::sin(yaw) + dz * std::cos(yaw)) / std::sqrt(distance) < 0.35) ||
                !ClearAttackLine(*_world3D, x, y + 18, z, monster.x, monster.y + 10, monster.z)) continue;
            best = distance; target = &monster;
        }
        if (target)
        {
            event.damage = (std::min)(uint16_t{20}, target->hp); target->hp -= event.damage; ++target->revision;
            event.monsterId = target->id; event.hp = target->hp; event.maxHp = 60; event.revision = target->revision;
            event.result = target->hp ? BasicAttackResult::Hit : BasicAttackResult::Killed;
            if (!target->hp) target->respawnAt = now + std::chrono::seconds(5);
            _monsterPacketTick = UINT64_MAX;
        }
    }
    auto send = [&](Player& recipient)
    {
        event.generation = recipient.GetGeneration();
        PacketHeader header{uint16_t(sizeof(PacketHeader) + sizeof(event)), uint16_t(GamePacketOpcode::BasicAttackEvent3D)};
        char bytes[sizeof(header) + sizeof(event)]; memcpy(bytes, &header, sizeof(header)); memcpy(bytes + sizeof(header), &event, sizeof(event));
        return recipient.GetSession()->Send(bytes, sizeof(bytes));
    };
    if (!swing) return send(player);
    for (const auto& entry : _players)
        if (entry.second->GetSession() && !send(*entry.second)) entry.second->GetSession()->RequestClose();
    return true;
}
