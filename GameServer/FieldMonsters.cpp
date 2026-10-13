#include "Map.h"
#include "Player.h"
#include "../ServerCore/Session.h"
#include "../ServerCore/Packet.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <random>
#include <atomic>

namespace
{
    bool ClearAttackLine(const World3DDefinition& world, double px, double py, double pz, double mx, double my, double mz);
    uint64_t NewDropId()
    {
        static std::atomic<uint64_t> next{(uint64_t(std::random_device{}()) << 32) | std::random_device{}()};
        uint64_t id = ++next; return id ? id : ++next;
    }
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
bool Map::SendFieldMonsters(Player& recipient, bool force)
{
    if (!Is3D() || recipient.GetMap() != this || !recipient.GetSession()) return false;
    if (_monsterPacketTick != _tick)
    {
        _monsterParts.clear(); _monsterRecords.clear(); _monsterHashes.clear(); _monsterPacketTick = _tick;
        for (const auto& monster : _fieldMonsters)
        {
            _monsterRecords.push_back({monster.id, monster.kind, static_cast<int32_t>(std::llround(monster.x * 1000)),
                static_cast<int32_t>(std::llround(monster.y * 1000)), static_cast<int32_t>(std::llround(monster.z * 1000)),
                monster.yaw, monster.hp, 60, monster.revision});
            const auto& record = _monsterRecords.back(); uint64_t hash = 1469598103934665603ULL;
            for (uint64_t value : {uint64_t(record.monsterId), uint64_t(record.x), uint64_t(record.y), uint64_t(record.z), uint64_t(record.yaw), uint64_t(record.hp), record.revision})
            { hash ^= value; hash *= 1099511628211ULL; }
            _monsterHashes.push_back(hash);
        }
    }
    std::vector<uint32_t> ids;
    ids.reserve(_monsterRecords.size());
    const auto& pose = recipient.GetState3D();
    uint64_t hash = 1469598103934665603ULL;
    for (uint32_t index = 0; index < _monsterRecords.size(); ++index)
    {
        const auto& record = _monsterRecords[index];
        double dx = (record.x - double(pose.x)) / 1000.0, dz = (record.z - double(pose.z)) / 1000.0;
        // 작은 샘플 필드는 전체 표시를 유지하고 32마리 초과부터 반경 16m만 전달한다.
        if (_fieldMonsters.size() > FIELD_MONSTERS_PER_PART && dx * dx + dz * dz > 320.0 * 320.0) continue;
        ids.push_back(index); hash ^= _monsterHashes[index]; hash *= 1099511628211ULL;
    }
    auto& view = _monsterViews[recipient.GetCharacterId()];
    if (!force && view.sent && view.hash == hash && _fieldNow < view.heartbeat) return true;
    auto found = _monsterParts.find(ids);
    if (found == _monsterParts.end())
    {
        std::vector<std::vector<char>> packets;
        const auto total = static_cast<uint16_t>(ids.size());
        const auto parts = static_cast<uint16_t>((std::max)(size_t{1}, (ids.size() + FIELD_MONSTERS_PER_PART - 1) / FIELD_MONSTERS_PER_PART));
        for (uint16_t part = 0; part < parts; ++part)
        {
            size_t first = part * FIELD_MONSTERS_PER_PART;
            auto count = static_cast<uint16_t>((std::min)(size_t{FIELD_MONSTERS_PER_PART}, ids.size() - first));
            FieldMonsterPartsHeader payload; payload.mapId = GetMapId(); payload.tick = _tick;
            payload.count = count; payload.part = part; payload.parts = parts; payload.total = total;
            PacketHeader header{uint16_t(sizeof(PacketHeader) + sizeof(payload) + count * sizeof(FieldMonsterRecord)), uint16_t(GamePacketOpcode::FieldMonsters3D)};
            packets.emplace_back(header.size); auto& packet = packets.back();
            memcpy(packet.data(), &header, sizeof(header)); memcpy(packet.data() + sizeof(header), &payload, sizeof(payload));
            for (uint16_t i = 0; i < count; ++i)
                memcpy(packet.data() + sizeof(header) + sizeof(payload) + i * sizeof(FieldMonsterRecord), &_monsterRecords[ids[first + i]], sizeof(FieldMonsterRecord));
        }
        found = _monsterParts.emplace(std::move(ids), std::move(packets)).first; ++_monsterMetrics.builds;
    }
    uint64_t generation = recipient.GetGeneration();
    for (auto& packet : found->second)
    {
        memcpy(packet.data() + sizeof(PacketHeader) + offsetof(FieldMonstersHeader, generation), &generation, sizeof(generation));
        if (!recipient.GetSession()->Send(packet.data(), static_cast<int32_t>(packet.size()))) { ++_monsterMetrics.failures; return false; }
        ++_monsterMetrics.packets; _monsterMetrics.bytes += packet.size();
    }
    view.sent = true; view.hash = hash; view.heartbeat = _fieldNow + std::chrono::seconds(1);
    return true;
}
void Map::TickFieldMonsters(uint64_t, std::chrono::steady_clock::time_point now)
{
    _fieldNow = now;
    bool removed = false;
    _fieldDrops.erase(std::remove_if(_fieldDrops.begin(), _fieldDrops.end(), [&](auto& drop)
    {
        if (drop.reservedBy && now >= drop.reservationUntil) drop.reservedBy = 0;
        if (now < drop.expires || drop.reservedBy) return false;
        removed = true; return true;
    }), _fieldDrops.end());
    if (removed) { ++_dropRevision; BroadcastDrops(); }
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
            if (now >= monster.respawnAt) { monster.hp = 60; ++monster.revision; monster.x=(monster.left+monster.right)/2; monster.z=monster.homeZ; monster.rest = 0.8; _monsterPacketTick = UINT64_MAX; }
            continue;
        }
        Player* victim = nullptr; double closest = 120.0 * 120.0;
        for (const auto& entry : _players)
        {
            auto& player = *entry.second; const auto& pose = player.GetState3D();
            double dx = pose.x / 1000.0 - monster.x, dz = pose.z / 1000.0 - monster.z, distance = dx * dx + dz * dz;
            if (!player.CanAttack() || std::abs(pose.y / 1000.0 - monster.y) > 30 || distance > closest ||
                !ClearAttackLine(*_world3D, monster.x, monster.y + 10, monster.z, pose.x / 1000.0, pose.y / 1000.0 + 18, pose.z / 1000.0)) continue;
            victim = &player; closest = distance;
        }
        if (victim)
        {
            const auto& pose = victim->GetState3D(); double dx = pose.x / 1000.0 - monster.x, dz = pose.z / 1000.0 - monster.z;
            monster.direction = dx >= 0 ? 1 : -1; monster.rest = 0;
            monster.yaw=static_cast<uint16_t>(std::fmod(std::atan2(dx,dz)*18000/3.141592653589793+36000,36000));
            // 1/1000 좌표의 반올림 차이로 사거리 경계에서 추적만 반복하지 않도록 작은 여유를 둔다.
            if (closest <= 24.1 * 24.1)
            {
                // 피격은 DB 작업에 등록한 경우에만 공격 간격을 소비하고 물약과 같은 순서로 확정한다.
                if (now >= monster.attackAt && victim->RequestDamage(victim->IncomingDamage())) monster.attackAt = now + std::chrono::milliseconds(1500);
            }
            else
            {
                double distance = std::sqrt(closest), amount = (std::min)(distance - 24, (18.0 + monster.kind * 3) * delta);
                MovementAction3D probe; probe.x = static_cast<int64_t>((monster.x + dx / distance * amount) * 1000);
                probe.y = static_cast<int64_t>(monster.y * 1000); probe.z = static_cast<int64_t>((monster.z + dz / distance * amount) * 1000); probe.supportId = 1;
                if (CanStand(*_world3D, probe)) { monster.x = probe.x / 1000.0; monster.z = probe.z / 1000.0; }
            }
            continue;
        }
        if (monster.x<monster.left || monster.x>monster.right || std::abs(monster.z-monster.homeZ)>0.01)
        {
            double dx=std::clamp(monster.x,monster.left,monster.right)-monster.x, dz=monster.homeZ-monster.z, distance=std::sqrt(dx*dx+dz*dz);
            double amount=(std::min)(distance,18.0*delta);
            if (distance>0.0001)
            {
                MovementAction3D probe; probe.x=static_cast<int64_t>((monster.x+dx/distance*amount)*1000); probe.z=static_cast<int64_t>((monster.z+dz/distance*amount)*1000); probe.y=static_cast<int64_t>(monster.y*1000); probe.supportId=1;
                if (CanStand(*_world3D,probe)) { monster.x=probe.x/1000.0; monster.z=probe.z/1000.0; }
            }
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
        monster.yaw=static_cast<uint16_t>(monster.direction>0 ? 9000 : 27000);
    }
    // 서버에서 계산한 상태를 200ms마다 같은 필드의 모든 캐릭터에게 전달한다.
    if (now < _monsterBroadcast) return;
    _monsterBroadcast = now + std::chrono::milliseconds(200);
    // 이동으로 주변 목록이 달라지는 경우도 확인하고 같은 상태는 1초 확인 주기만 유지한다.
    for (const auto& entry : _players)
        if (entry.second->GetSession() && (!SendFieldMonsters(*entry.second, false) || !SendFieldDrops(*entry.second))) entry.second->GetSession()->RequestClose();
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
            if (!monster.hp || std::abs(monster.y - y) > 30 || distance > best || (target && distance>=best-0.01) ||
                (distance > 0.000001 && (dx * std::sin(yaw) + dz * std::cos(yaw)) / std::sqrt(distance) < 0.35) ||
                !ClearAttackLine(*_world3D, x, y + 18, z, monster.x, monster.y + 10, monster.z)) continue;
            best = distance; target = &monster;
        }
        if (target)
        {
            event.damage = (std::min)(player.AttackDamage(), target->hp); target->hp -= event.damage; ++target->revision;
            event.monsterId = target->id; event.hp = target->hp; event.maxHp = 60; event.revision = target->revision;
            event.result = target->hp ? BasicAttackResult::Hit : BasicAttackResult::Killed;
            if (!target->hp) { target->respawnAt = now + std::chrono::seconds(5); CreateDrop(*target, now); }
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
    BroadcastDrops();
    return true;
}

bool Map::SendVitals(Player& player, uint16_t damage, bool resync)
{
    const auto& state = player.GetVitals();
    VitalsPacket payload{GetMapId(), 0, player.GetCharacterId(), state.revision, state.hp, state.maxHp, state.mp, state.maxMp, damage, uint8_t(player.CombatPaused())};
    PacketHeader header{uint16_t(sizeof(PacketHeader) + sizeof(payload)), uint16_t(GamePacketOpcode::Vitals3D)};
    for (const auto& entry : _players) if (entry.second->GetSession())
    {
        payload.generation = entry.second->GetGeneration(); char bytes[sizeof(header) + sizeof(payload)];
        memcpy(bytes,&header,sizeof(header)); memcpy(bytes+sizeof(header),&payload,sizeof(payload));
        if (!entry.second->GetSession()->Send(bytes,sizeof(bytes))) entry.second->GetSession()->RequestClose();
        // 복구 중단·재개에서는 상대 화면도 같은 확정 위치로 맞춘다. 일반 피격에는 추가하지 않는다.
        else if ((resync || payload.paused) && !SendState3D(*entry.second, player)) entry.second->GetSession()->RequestClose();
    }
    return true;
}
void Map::CreateDrop(const FieldMonster& monster, std::chrono::steady_clock::time_point now)
{
    if (_fieldDrops.size() >= 64) return;
    DropRecord record{NewDropId(), 101, 1, static_cast<int32_t>(monster.x * 1000), static_cast<int32_t>(monster.y * 1000), static_cast<int32_t>(monster.z * 1000)};
    _fieldDrops.push_back({record, 0, now + std::chrono::seconds(60), {}}); ++_dropRevision;
}
bool Map::SendFieldDrops(Player& recipient)
{
    if (!Is3D() || recipient.GetMap() != this || !recipient.GetSession()) return false;
    std::vector<uint64_t> ids; std::vector<DropRecord> records;
    const auto& pose = recipient.GetState3D();
    for (const auto& drop : _fieldDrops)
    {
        double dx = (drop.record.x - double(pose.x)) / 1000.0, dz = (drop.record.z - double(pose.z)) / 1000.0;
        if (dx * dx + dz * dz > 320.0 * 320.0) continue;
        ids.push_back(drop.record.id); records.push_back(drop.record);
    }
    auto& view = _dropViews[recipient.GetCharacterId()];
    if (view.sent && view.ids == ids) return true;
    // 수신자별 변경 번호로 주변 목록의 진입·이탈도 전체 목록 교체로 전달한다.
    FieldMonstersHeader payload{GetMapId(), recipient.GetGeneration(), view.revision + 1, static_cast<uint16_t>(records.size())};
    PacketHeader header{uint16_t(sizeof(PacketHeader) + sizeof(payload) + payload.count * sizeof(DropRecord)), uint16_t(GamePacketOpcode::FieldDrops3D)};
    std::vector<char> packet(header.size); memcpy(packet.data(), &header, sizeof(header)); memcpy(packet.data() + sizeof(header), &payload, sizeof(payload));
    if (!records.empty()) memcpy(packet.data() + sizeof(header) + sizeof(payload), records.data(), records.size() * sizeof(DropRecord));
    if (!recipient.GetSession()->Send(packet.data(), static_cast<int32_t>(packet.size()))) return false;
    view.ids = std::move(ids); view.sent = true; ++view.revision;
    return true;
}
void Map::BroadcastDrops()
{
    // 생성·획득·만료를 즉시 알리고 주변 목록이 같으면 송신하지 않는다.
    for (const auto& entry : _players) if (entry.second->GetSession() && !SendFieldDrops(*entry.second)) entry.second->GetSession()->RequestClose();
}
PickupResult Map::ReserveDrop(Player& player, const PickupRequest& request, InventoryMutation& mutation)
{
    if (request.mapId != GetMapId() || request.generation != player.GetGeneration() || !player.CanAttack()) return PickupResult::NotFound;
    auto now = std::chrono::steady_clock::now();
    for (auto& drop : _fieldDrops) if (drop.record.id == request.dropId && now < drop.expires)
    {
        if (drop.reservedBy) return PickupResult::Busy;
        const auto& pose = player.GetState3D(); double dx = pose.x/1000.0-drop.record.x/1000.0, dz=pose.z/1000.0-drop.record.z/1000.0;
        if (dx*dx+dz*dz > 40*40 || std::abs(pose.y-drop.record.y) > 30000 ||
            !ClearAttackLine(*_world3D,pose.x/1000.0,pose.y/1000.0+18,pose.z/1000.0,drop.record.x/1000.0,drop.record.y/1000.0+10,drop.record.z/1000.0)) return PickupResult::OutOfRange;
        drop.reservedBy = player.GetCharacterId(); drop.reservationUntil = now + std::chrono::seconds(8);
        mutation.dropId=drop.record.id; mutation.itemId=drop.record.itemId; mutation.quantity=drop.record.quantity; return PickupResult::Success;
    }
    return PickupResult::NotFound;
}
void Map::FinishDrop(uint64_t id, uint32_t owner, InventoryResult result)
{
    for (auto it=_fieldDrops.begin();it!=_fieldDrops.end();++it) if (it->record.id==id && it->reservedBy==owner)
    {
        if (result==InventoryResult::Success || result==InventoryResult::AlreadyTaken) { _fieldDrops.erase(it); ++_dropRevision; BroadcastDrops(); }
        else it->reservedBy=0;
        return;
    }
}
