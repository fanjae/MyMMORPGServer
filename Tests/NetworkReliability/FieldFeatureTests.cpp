#include "../../ServerCore/Session.h"
#include "../../ServerCore/Packet.h"
#include "../../GameServer/InventoryService.h"
#include "TestInventoryStore.h"
#include "../../GameServer/Map.h"
#include "../../GameServer/Player.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <thread>
#include <fstream>

namespace
{
    void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    struct Capture : Session
    {
        Capture() : Session(INVALID_SOCKET) {}
        std::vector<std::vector<char>> packets; bool fail = false;
        bool Send(const char* data, int32_t size) override { if (fail) return false; packets.emplace_back(data, data + size); return true; }
        bool OnPacket(uint16_t, const char*, uint16_t) override { return true; }
    };
}
void RunFieldFeatureTests()
{
    // DB 저장과 분리해 슬롯 조작, 중복 요청, 캐릭터별 상태 구분을 확인한다.
    TestInventoryStore service;
    InventoryRequestPacket request;
    auto original = service.Apply(1001, request); Check(original.result == InventoryResult::Success && original.entries[24].quantity == 10, "Starter inventory missing");
    request = {InventoryOperation::Move, 1, 4, 0, original.revision};
    auto merged = service.Apply(1001, request);
    Check(merged.entries[24].quantity == 15 && !merged.entries[28].itemId, "Inventory merge lost quantities");
    auto duplicate = service.Apply(1001, request);
    Check(duplicate.result == InventoryResult::StaleRevision && duplicate.entries[24].quantity == 15 && duplicate.revision == merged.revision, "Duplicate inventory request applied twice");
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    request = {InventoryOperation::Use, 1, 0, 0, merged.revision}; auto used = service.Apply(1001, request);
    Check(used.hp == 80 && used.entries[24].quantity == 14, "Potion use did not atomically change quantity and HP");
    Check(service.Apply(2001, {}).entries[24].quantity == 10, "Inventory owners were not isolated");
    request = {InventoryOperation::Move, 5, 0, 0, used.revision};
    Check(service.Apply(1001, request).result == InventoryResult::InvalidSlot, "Out of range inventory category accepted");
    std::cout << "[PASS] Inventory rules: merge, stale request rejection, potion use and owner isolation\n";

    auto invalid = used; invalid.entries[0].quantity = 200;
    Check(InventoryService::Apply(invalid, {}).result == InventoryResult::StorageError, "Corrupt state silently accepted");
    request = {InventoryOperation::Use, 1, 0, 0, used.revision};
    auto full = InventoryService::Apply(used, request); request.revision = full.revision;
    auto noOp = InventoryService::Apply(full, request);
    Check(full.hp == full.maxHp && noOp.result == InventoryResult::CannotUse && noOp.entries[24].quantity == full.entries[24].quantity,
        "Full health potion consumed inventory");
    std::cout << "[PASS] Inventory rules reject corrupt states and full-health consumption\n";
    auto limited = original; limited.entries[24].quantity = 100; limited.entries[28].quantity = 100;
    auto unchanged = InventoryService::Apply(limited, {InventoryOperation::Move, 1, 4, 0, limited.revision});
    Check(unchanged.revision == limited.revision && unchanged.entries[28].quantity == 100, "Full stack merge lost quantity or changed revision");
    std::cout << "[PASS] Full stack merge preserves quantity and skips unnecessary mutation\n";

    // 같은 필드의 두 캐릭터는 같은 순찰 상태를 받고 다른 필드에는 전달되지 않아야 한다.
    World3DDefinition world; world.settings = {100000002, 0, 1, -400, -200, -400, 400, 400, 400, 6, 36, 80, 160, 400, 320, 0, 0, 0, 1};
    world.boxes.push_back({100000002, 0, 1, -400, -40, -400, 400, 0, 240});
    Map map({100000002, 0, 0, -400, 400, -200, 400, 80, 12}); map.SetWorld3D(world); map.SpawnFieldMonsters();
    Capture a, b, other; Player pa(1001, 1, "a", 1), pb(2001, 2, "b", 1), pc(3001, 3, "c", 1);
    pa.SetSession(&a); pb.SetSession(&b); pc.SetSession(&other); Check(map.AddPlayer(pa) && map.AddPlayer(pb), "Field entry failed");
    Map otherMap({100000003, 0, 0, -400, 400, -200, 400, 80, 12}); otherMap.SetWorld3D(world); otherMap.AddPlayer(pc);
    Check(map.SendFieldMonsters(pa), "Initial monster snapshot missing");
    FieldMonsterRecord initial; memcpy(&initial, a.packets.back().data() + sizeof(PacketHeader) + sizeof(FieldMonstersHeader), sizeof(initial));
    a.packets.clear(); auto now = std::chrono::steady_clock::now();
    for (uint64_t tick = 1; tick <= 60; ++tick) map.Tick(tick, now + std::chrono::milliseconds(tick * 20));
    Check(a.packets.size() == b.packets.size() && a.packets.size() <= 6 && other.packets.empty(), "Monster broadcast cadence or map isolation failed");
    Check(a.packets.back() == b.packets.back(), "Two field occupants received different authoritative monster states");
    FieldMonsterRecord moved; memcpy(&moved, a.packets.back().data() + sizeof(PacketHeader) + sizeof(FieldMonstersHeader), sizeof(moved));
    Check(moved.monsterId == initial.monsterId && moved.x > initial.x && moved.y == initial.y, "Monster did not patrol on the floor");
    map.RemovePlayer(pb); size_t previous = b.packets.size(); map.Tick(70, now + std::chrono::milliseconds(1400));
    Check(b.packets.size() == previous, "Departed player received field monsters");
    Check(map.AddPlayer(pb) && map.SendFieldMonsters(pb), "Reentered player missing current field state");
    FieldMonstersHeader header; memcpy(&header, b.packets.back().data() + sizeof(PacketHeader), sizeof(header));
    Check(header.generation == pb.GetGeneration() && header.count == 3, "Reentry monster generation/count wrong");
    b.fail = true; map.Tick(90, now + std::chrono::milliseconds(1800)); Check(b.IsCloseRequested() && !a.IsCloseRequested(), "Monster send failure affected healthy peer");
    std::cout << "[PASS] Shared monster patrol, bounded broadcasts, map isolation, reentry and send failure isolation\n";


    // 서버의 확정 위치·방향·행동 번호에서만 피해를 계산하고 같은 필드에 결과를 공유한다.
    {
        Map arena({100000002, 0, 0, -400, 400, -200, 400, 80, 12}); arena.SetWorld3D(world); arena.SpawnFieldMonsters(1);
        Capture attacker, observer; Player hero(8001, 8, "hero", 1), peer(8002, 9, "peer", 1);
        hero.SetSession(&attacker); peer.SetSession(&observer); arena.AddPlayer(hero); arena.AddPlayer(peer);
        hero.SetVitals(InventoryService::Starter());
        auto state = hero.GetState3D(); state.x = -160000; state.z = -130000; state.y = 0; state.yaw = 0; hero.SetState3D(state);
        BasicAttackRequest attackRequest{arena.GetMapId(), hero.GetGeneration(), 1, hero.GetActionSequence()};
        auto send = [&](uint64_t sequence, int milliseconds)
        {
            attackRequest.sequence = sequence; Check(arena.AttackField(hero, attackRequest, now + std::chrono::milliseconds(milliseconds)), "Attack send failed");
            BasicAttackEvent event; memcpy(&event, attacker.packets.back().data() + sizeof(PacketHeader), sizeof(event)); return event;
        };
        auto hit = send(1, 0); Check(hit.result == BasicAttackResult::Hit && hit.hp == 40 && hit.damage == 20 &&
            observer.packets.back() == attacker.packets.back(), "Hit not shared or damage wrong");
        auto duplicateAttack = send(1, 600); Check(duplicateAttack.result == BasicAttackResult::InvalidState, "Duplicate attack applied");
        auto cooldown = send(2, 100); Check(cooldown.result == BasicAttackResult::Cooldown, "Attack cooldown bypassed");
        attackRequest.generation = hero.GetGeneration() + 1; Check(send(3, 600).result == BasicAttackResult::InvalidState, "Old map attack accepted"); attackRequest.generation = hero.GetGeneration();
        state.yaw = 18000; hero.SetState3D(state); Check(send(3, 600).result == BasicAttackResult::Miss, "Back-facing attack hit");
        state.yaw = 0; state.z = -300000; hero.SetState3D(state); Check(send(4, 1200).result == BasicAttackResult::Miss, "Out-of-range attack hit");
        state.z = -130000; hero.SetState3D(state); Check(send(5, 1800).hp == 20 && send(6, 2400).result == BasicAttackResult::Killed, "Monster death missing");
        Check(send(7, 3000).result == BasicAttackResult::Miss, "Dead monster attacked");
        arena.Tick(400, now + std::chrono::seconds(8)); arena.SendFieldMonsters(hero);
        FieldMonsterRecord respawn; memcpy(&respawn, attacker.packets.back().data() + sizeof(PacketHeader) + sizeof(FieldMonstersHeader), sizeof(respawn));
        Check(respawn.hp == 60 && respawn.revision == 5, "Monster respawn reset revision or HP incorrectly");
        World3DDefinition blocked = world; blocked.boxes.push_back({100000002, 0, 2, -180, 0, -120, -140, 60, -115});
        Map wall({100000002, 0, 0, -400, 400, -200, 400, 80, 12}); wall.SetWorld3D(blocked); wall.SpawnFieldMonsters(1); arena.RemovePlayer(hero); wall.AddPlayer(hero); hero.SetState3D(state);
        attackRequest.mapId = wall.GetMapId(); attackRequest.generation = hero.GetGeneration(); attackRequest.sequence = 8;
        wall.AttackField(hero, attackRequest, now + std::chrono::seconds(9));
        BasicAttackEvent missed; memcpy(&missed, attacker.packets.back().data() + sizeof(PacketHeader), sizeof(missed)); Check(missed.result == BasicAttackResult::Miss, "Attack crossed a wall");
        auto dead = InventoryService::Starter(); dead.hp = 0; hero.SetVitals(dead); attackRequest.sequence = 9;
        wall.AttackField(hero, attackRequest, now + std::chrono::seconds(10)); memcpy(&missed, attacker.packets.back().data() + sizeof(PacketHeader), sizeof(missed));
        Check(missed.result == BasicAttackResult::Unavailable, "Zero-HP character attacked");
        std::cout << "[PASS] Basic attack damage, shared results, cooldown, duplicate/map/range/facing/wall guards, death and respawn\n";
    }

    // 수신자가 늘어도 공통 패킷 구성 횟수는 방송 주기 수와 같아야 한다.
    for (uint16_t count : {uint16_t{3}, uint16_t{32}, uint16_t{128}})
    {
        Map load({100000002, 0, 0, -400, 400, -200, 400, 80, 12}); load.SetWorld3D(world); load.SpawnFieldMonsters(count);
        std::vector<std::unique_ptr<Capture>> peers;
        std::vector<std::unique_ptr<Player>> players;
        for (uint32_t i = 0; i < 32; ++i)
        {
            peers.push_back(std::make_unique<Capture>()); players.push_back(std::make_unique<Player>(i + 1, i + 1, "load", uint16_t{1}));
            players.back()->SetSession(peers.back().get()); Check(load.AddPlayer(*players.back()), "Load entry failed");
        }
        auto started = std::chrono::steady_clock::now();
        for (uint64_t tick = 1; tick <= 100; ++tick) load.Tick(tick, now + std::chrono::milliseconds(tick * 20));
        auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        auto metrics = load.TakeFieldMonsterMetrics();
        Check(metrics.packets == metrics.builds * peers.size() && metrics.builds == 10 && metrics.failures == 0,
            "Monster packet rebuilt per recipient or cadence changed");
        FieldMonstersHeader snapshot; memcpy(&snapshot, peers.front()->packets.back().data() + sizeof(PacketHeader), sizeof(snapshot));
        Check(snapshot.count == count && peers.front()->packets.back().size() <= MAX_PACKET_SIZE, "Monster load packet count/size invalid");
        std::cout << "[PASS] Monster load count=" << count << " peers=32 builds=" << metrics.builds << " packets=" << metrics.packets
            << " bytes=" << metrics.bytes << " cpuMs=" << elapsed << '\n';
        for (auto& player : players) load.RemovePlayer(*player);
        load.Tick(200, now + std::chrono::seconds(4));
        Check(load.TakeFieldMonsterMetrics().simulated == 0, "Unoccupied field continued monster simulation");
    }

}
