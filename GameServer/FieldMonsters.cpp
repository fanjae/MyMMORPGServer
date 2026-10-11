#include "Map.h"
#include "Player.h"
#include "../ServerCore/Session.h"
#include "../ServerCore/Packet.h"
#include <algorithm>
#include <cmath>
#include <cstring>

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

void Map::SpawnFieldMonsters()
{
    if (!Is3D() || !_fieldMonsters.empty()) return;
    // 샘플의 넓은 바닥에 세 마리를 배치하고 지형과 겹치는 시작 위치는 제외한다.
    for (uint32_t i = 0; i < 3; ++i)
    {
        double x = -160.0 + i * 25, z = -100.0 + i * 55;
        MovementAction3D probe;
        probe.x = static_cast<int64_t>(x * 1000); probe.z = static_cast<int64_t>(z * 1000);
        probe.supportId = 1;
        if (CanStand(*_world3D, probe)) _fieldMonsters.push_back({i + 1, static_cast<uint8_t>(i), x, 0, z, x - 35, x + 35});
    }
}
bool Map::SendFieldMonsters(Player& recipient)
{
    if (!Is3D() || recipient.GetMap() != this || !recipient.GetSession()) return false;
    // 위치는 Map의 공통 상태를 사용하고 입장 번호만 수신자의 현재 값으로 기록한다.
    FieldMonstersHeader payload{GetMapId(), recipient.GetGeneration(), _tick, static_cast<uint16_t>(_fieldMonsters.size())};
    PacketHeader header{static_cast<uint16_t>(sizeof(PacketHeader) + sizeof(payload) + payload.count * sizeof(FieldMonsterRecord)),
        static_cast<uint16_t>(GamePacketOpcode::FieldMonsters3D)};
    std::vector<char> bytes(header.size);
    memcpy(bytes.data(), &header, sizeof(header)); memcpy(bytes.data() + sizeof(header), &payload, sizeof(payload));
    for (size_t i = 0; i < _fieldMonsters.size(); ++i)
    {
        const auto& monster = _fieldMonsters[i];
        FieldMonsterRecord record{monster.id, monster.kind, static_cast<int32_t>(std::llround(monster.x * 1000)),
            static_cast<int32_t>(std::llround(monster.y * 1000)), static_cast<int32_t>(std::llround(monster.z * 1000)),
            static_cast<uint16_t>(monster.direction > 0 ? 9000 : 27000)};
        memcpy(bytes.data() + sizeof(header) + sizeof(payload) + i * sizeof(record), &record, sizeof(record));
    }
    return recipient.GetSession()->Send(bytes.data(), static_cast<int32_t>(bytes.size()));
}
void Map::TickFieldMonsters(uint64_t, std::chrono::steady_clock::time_point now)
{
    if (_fieldMonsters.empty()) return;
    // 서버 처리가 지연되어도 한 번에 이동할 시간을 제한해 순찰 위치가 크게 뛰지 않도록 한다.
    double delta = _monsterUpdated == std::chrono::steady_clock::time_point{} ? 0 :
        std::clamp(std::chrono::duration<double>(now - _monsterUpdated).count(), 0.0, 0.1);
    _monsterUpdated = now;
    for (auto& monster : _fieldMonsters)
    {
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
