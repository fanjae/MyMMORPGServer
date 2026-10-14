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
    const std::vector<char>& Last(Capture& session,GamePacketOpcode opcode)
    {
        for (auto it=session.packets.rbegin();it!=session.packets.rend();++it)
            if (reinterpret_cast<const PacketHeader*>(it->data())->opcode==static_cast<uint16_t>(opcode)) return *it;
        throw std::runtime_error("Expected field packet missing");
    }
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
    FieldMonsterRecord initial; memcpy(&initial, a.packets.back().data() + sizeof(PacketHeader) + sizeof(FieldMonsterPartsHeader), sizeof(initial));
    a.packets.clear(); auto now = std::chrono::steady_clock::now();
    for (uint64_t tick = 1; tick <= 60; ++tick) map.Tick(tick, now + std::chrono::milliseconds(tick * 20));
    Check(a.packets.size() == b.packets.size() && a.packets.size() <= 7 && other.packets.empty(), "Monster broadcast cadence or map isolation failed");
    Check(a.packets.back() == b.packets.back(), "Two field occupants received different authoritative monster states");
    FieldMonsterRecord moved; memcpy(&moved, a.packets.back().data() + sizeof(PacketHeader) + sizeof(FieldMonsterPartsHeader), sizeof(moved));
    Check(moved.monsterId == initial.monsterId && moved.x > initial.x && moved.y == initial.y, "Monster did not patrol on the floor");
    map.RemovePlayer(pb); size_t previous = b.packets.size(); map.Tick(70, now + std::chrono::milliseconds(1400));
    Check(b.packets.size() == previous, "Departed player received field monsters");
    Check(map.AddPlayer(pb) && map.SendFieldMonsters(pb), "Reentered player missing current field state");
    FieldMonsterPartsHeader header; memcpy(&header, b.packets.back().data() + sizeof(PacketHeader), sizeof(header));
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
            BasicAttackEvent event; memcpy(&event, Last(attacker,GamePacketOpcode::BasicAttackEvent3D).data() + sizeof(PacketHeader), sizeof(event)); return event;
        };
        auto hit = send(1, 0); Check(hit.result == BasicAttackResult::Hit && hit.hp == 40 && hit.damage == 20 &&
            Last(observer,GamePacketOpcode::BasicAttackEvent3D) == Last(attacker,GamePacketOpcode::BasicAttackEvent3D), "Hit not shared or damage wrong");
        auto duplicateAttack = send(1, 600); Check(duplicateAttack.result == BasicAttackResult::InvalidState, "Duplicate attack applied");
        auto cooldown = send(2, 100); Check(cooldown.result == BasicAttackResult::Cooldown, "Attack cooldown bypassed");
        attackRequest.generation = hero.GetGeneration() + 1; Check(send(3, 600).result == BasicAttackResult::InvalidState, "Old map attack accepted"); attackRequest.generation = hero.GetGeneration();
        state.yaw = 18000; hero.SetState3D(state); Check(send(3, 600).result == BasicAttackResult::Miss, "Back-facing attack hit");
        state.yaw = 0; state.z = -300000; hero.SetState3D(state); Check(send(4, 1200).result == BasicAttackResult::Miss, "Out-of-range attack hit");
        state.z = -130000; hero.SetState3D(state); Check(send(5, 1800).hp == 20 && send(6, 2400).result == BasicAttackResult::Killed, "Monster death missing");
        Check(send(7, 3000).result == BasicAttackResult::Miss, "Dead monster attacked");
        arena.Tick(400, now + std::chrono::seconds(8)); arena.SendFieldMonsters(hero);
        FieldMonsterRecord respawn; memcpy(&respawn, attacker.packets.back().data() + sizeof(PacketHeader) + sizeof(FieldMonsterPartsHeader), sizeof(respawn));
        Check(respawn.hp == 60 && respawn.revision == 5, "Monster respawn reset revision or HP incorrectly");
        World3DDefinition blocked = world; blocked.boxes.push_back({100000002, 0, 2, -180, 0, -120, -140, 60, -115});
        Map wall({100000002, 0, 0, -400, 400, -200, 400, 80, 12}); wall.SetWorld3D(blocked); wall.SpawnFieldMonsters(1); arena.RemovePlayer(hero); wall.AddPlayer(hero); hero.SetState3D(state);
        attackRequest.mapId = wall.GetMapId(); attackRequest.generation = hero.GetGeneration(); attackRequest.sequence = 8;
        wall.AttackField(hero, attackRequest, now + std::chrono::seconds(9));
        BasicAttackEvent missed; memcpy(&missed, Last(attacker,GamePacketOpcode::BasicAttackEvent3D).data() + sizeof(PacketHeader), sizeof(missed)); Check(missed.result == BasicAttackResult::Miss, "Attack crossed a wall");
        auto dead = InventoryService::Starter(); dead.hp = 0; hero.SetVitals(dead); attackRequest.sequence = 9;
        wall.AttackField(hero, attackRequest, now + std::chrono::seconds(10)); memcpy(&missed, attacker.packets.back().data() + sizeof(PacketHeader), sizeof(missed));
        Check(missed.result == BasicAttackResult::Unavailable, "Zero-HP character attacked");
        std::cout << "[PASS] Basic attack damage, shared results, cooldown, duplicate/map/range/facing/wall guards, death and respawn\n";
    }

    // 공중 공격도 높이 차이의 포함 경계를 같은 서버 좌표 정밀도로 판정한다.
    {
        Map arena({100000002,0,0,-400,400,-200,400,80,12}); arena.SetWorld3D(world); arena.SpawnFieldMonsters(1);
        Capture capture; Player airborne(8050,8,"airborne",1); airborne.SetSession(&capture); arena.AddPlayer(airborne); airborne.SetVitals(InventoryService::Starter());
        auto pose=airborne.GetState3D(); pose.x=-160000; pose.z=-130000; pose.y=30000; pose.yaw=0; pose.grounded=0; pose.supportId=0; airborne.SetState3D(pose);
        arena.AttackField(airborne,{arena.GetMapId(),airborne.GetGeneration(),1,0},now);
        BasicAttackEvent event; memcpy(&event,Last(capture,GamePacketOpcode::BasicAttackEvent3D).data()+sizeof(PacketHeader),sizeof(event));
        Check(event.result==BasicAttackResult::Hit && event.hp==40,"Air attack rejected the inclusive height boundary");
        ++pose.y; airborne.SetState3D(pose); arena.AttackField(airborne,{arena.GetMapId(),airborne.GetGeneration(),2,0},now+std::chrono::milliseconds(600));
        memcpy(&event,Last(capture,GamePacketOpcode::BasicAttackEvent3D).data()+sizeof(PacketHeader),sizeof(event));
        Check(event.result==BasicAttackResult::Miss,"Air attack crossed the height boundary");
        std::cout << "[PASS] Air attack includes 1.5m height difference and rejects the next coordinate unit\n";
    }

    // 장비와 전투는 입력 상태 대신 확정한 인벤토리·필드 상태에서 판정한다.
    {
        auto starter=InventoryService::Starter();
        auto equipped=InventoryService::Apply(starter,{InventoryOperation::Equip,0,0,0,starter.revision});
        Check(equipped.equipped[0].instanceId==starter.entries[0].instanceId && !equipped.entries[0].itemId,"Equipment identity lost");
        auto forged=equipped; forged.entries[4]=equipped.equipped[0]; Check(!InventoryService::Validate(forged),"Duplicate instance accepted");
        InventoryMutation damage; damage.damage=10; auto hurt=InventoryService::Mutate(starter,damage);
        Check(hurt.hp==40 && hurt.entries[24].quantity==10,"Damage changed slots");
        Map arena({100000002,0,0,-400,400,-200,400,80,12},2); arena.SetWorld3D(world); arena.SpawnFieldMonsters(1);
        Capture capture; Player victim(9001,9,"victim",1); victim.SetSession(&capture); arena.AddPlayer(victim); victim.SetVitals(starter);
        auto pose=victim.GetState3D(); pose.x=-160000; pose.z=-100000; victim.SetState3D(pose); int attacks=0;
        victim.SetDamageHandler([&](uint16_t amount){Check(amount==10,"Monster damage incorrect"); ++attacks; return true;});
        arena.Tick(1,now); arena.Tick(2,now+std::chrono::milliseconds(100)); arena.Tick(80,now+std::chrono::milliseconds(1600));
        Check(attacks==2,"Monster attack interval bypassed");
        auto dead=starter; dead.hp=0; victim.SetVitals(dead); arena.Tick(170,now+std::chrono::seconds(4)); Check(attacks==2,"Dead player attacked");
        Check(victim.GetMap()->GetChannel()==2 && sizeof(VitalsPacket)==35,"Channel or compact vitals invalid");
        std::cout << "[PASS] Equipment identity, duplicate rejection, monster detection/cooldown and dead target exclusion\n";
    }

    // 넓은 필드는 주변 몬스터만 나누어 보내고 빈 주변 목록도 전달해 이전 표시를 지운다.
    {
        Map area({100000002,0,0,-400,400,-200,400,80,12}); area.SetWorld3D(world); area.SpawnFieldMonsters(128);
        Capture nearPeer, farPeer; Player nearPlayer(9100,9,"near",1), farPlayer(9101,9,"far",1);
        nearPlayer.SetSession(&nearPeer); farPlayer.SetSession(&farPeer); area.AddPlayer(nearPlayer); area.AddPlayer(farPlayer);
        auto pose = nearPlayer.GetState3D(); pose.x = -56000; pose.z = -50000; nearPlayer.SetState3D(pose);
        pose.x = 1000000; pose.z = 1000000; farPlayer.SetState3D(pose);
        Check(area.SendFieldMonsters(nearPlayer) && area.SendFieldMonsters(farPlayer), "Nearby split list was not sent");
        FieldMonsterPartsHeader first, empty;
        memcpy(&first, nearPeer.packets.front().data()+sizeof(PacketHeader), sizeof(first));
        memcpy(&empty, farPeer.packets.front().data()+sizeof(PacketHeader), sizeof(empty));
        Check(first.total>32 && first.total<128 && first.parts==nearPeer.packets.size() && empty.total==0 && empty.parts==1, "Nearby filter or split count wrong");
        size_t records=0;
        for (const auto& packet : nearPeer.packets)
        { FieldMonsterPartsHeader part; memcpy(&part,packet.data()+sizeof(PacketHeader),sizeof(part)); records+=part.count; Check(packet.size()<=1024 && part.tick==first.tick,"Split packet budget/tick invalid"); }
        Check(records==first.total, "Split list lost records");
        nearPlayer.SetState3D(pose); nearPeer.packets.clear(); area.SendFieldMonsters(nearPlayer);
        memcpy(&empty,nearPeer.packets.front().data()+sizeof(PacketHeader),sizeof(empty)); Check(empty.total==0,"Area exit retained monsters");
        nearPlayer.PauseCombat(true); nearPlayer.SetVitals(InventoryService::Starter());
        Check(!nearPlayer.CanAttack(), "Paused combat still permits attack");
        nearPlayer.PauseCombat(false); pose.x=-160000; pose.z=-130000; pose.yaw=0; nearPlayer.SetState3D(pose);
        for (uint64_t sequence=1;sequence<=3;++sequence)
            area.AttackField(nearPlayer,{area.GetMapId(),nearPlayer.GetGeneration(),sequence,0},now+std::chrono::milliseconds(sequence*600));
        FieldMonstersHeader visibleDrop, hiddenDrop;
        memcpy(&visibleDrop,Last(nearPeer,GamePacketOpcode::FieldDrops3D).data()+sizeof(PacketHeader),sizeof(visibleDrop));
        memcpy(&hiddenDrop,Last(farPeer,GamePacketOpcode::FieldDrops3D).data()+sizeof(PacketHeader),sizeof(hiddenDrop));
        Check(visibleDrop.count==1 && hiddenDrop.count==0,"Distant drop was transmitted outside the nearby range");
        pose.x=pose.z=1000000; nearPlayer.SetState3D(pose); area.SendFieldDrops(nearPlayer);
        FieldMonstersHeader departedDrop; memcpy(&departedDrop,Last(nearPeer,GamePacketOpcode::FieldDrops3D).data()+sizeof(PacketHeader),sizeof(departedDrop));
        size_t sent=nearPeer.packets.size(); area.SendFieldDrops(nearPlayer);
        Check(departedDrop.count==0 && departedDrop.tick>visibleDrop.tick && nearPeer.packets.size()==sent,"Drop area exit or unchanged-list suppression failed");
        DropRecord boundaryDrop;
        // 목록 경계의 1/1000 좌표 차이에서도 진입·이탈이 반영되고 같은 바깥 목록은 반복 전송하지 않는다.
        pose.x=-160000; pose.z=-130000; nearPlayer.SetState3D(pose); area.SendFieldDrops(nearPlayer);
        memcpy(&boundaryDrop,Last(nearPeer,GamePacketOpcode::FieldDrops3D).data()+sizeof(PacketHeader)+sizeof(FieldMonstersHeader),sizeof(boundaryDrop));
        for (int repeat=0;repeat<3;++repeat)
        {
            pose.x=boundaryDrop.x+320000; pose.z=boundaryDrop.z; nearPlayer.SetState3D(pose); area.SendFieldDrops(nearPlayer);
            FieldMonstersHeader entered; memcpy(&entered,Last(nearPeer,GamePacketOpcode::FieldDrops3D).data()+sizeof(PacketHeader),sizeof(entered));
            Check(entered.count==1,"Drop disappeared at the inclusive 16m boundary");
            ++pose.x; nearPlayer.SetState3D(pose); area.SendFieldDrops(nearPlayer);
            memcpy(&departedDrop,Last(nearPeer,GamePacketOpcode::FieldDrops3D).data()+sizeof(PacketHeader),sizeof(departedDrop));
            sent=nearPeer.packets.size(); area.SendFieldDrops(nearPlayer);
            Check(departedDrop.count==0 && nearPeer.packets.size()==sent,"Repeated drop boundary exit duplicated a snapshot");
        }
        auto includesFirstMonster=[&]()
        {
            for (const auto& packet : nearPeer.packets)
            {
                FieldMonsterPartsHeader header; memcpy(&header,packet.data()+sizeof(PacketHeader),sizeof(header));
                for (uint16_t i=0;i<header.count;++i)
                { FieldMonsterRecord record; memcpy(&record,packet.data()+sizeof(PacketHeader)+sizeof(header)+i*sizeof(record),sizeof(record)); if(record.monsterId==1) return true; }
            }
            return false;
        };
        for (int repeat=0;repeat<3;++repeat)
        {
            pose.x=-160000+320000; pose.z=-100000; nearPlayer.SetState3D(pose); nearPeer.packets.clear(); area.SendFieldMonsters(nearPlayer,false);
            Check(includesFirstMonster(),"Monster disappeared at the inclusive 16m boundary");
            ++pose.x; nearPlayer.SetState3D(pose); nearPeer.packets.clear(); area.SendFieldMonsters(nearPlayer,false);
            Check(!nearPeer.packets.empty() && !includesFirstMonster(),"Monster survived the 16m boundary exit");
            nearPeer.packets.clear(); area.SendFieldMonsters(nearPlayer,false); Check(nearPeer.packets.empty(),"Unchanged monster boundary list was resent");
        }
        std::cout << "[PASS] Nearby monster lists split within 1024 bytes, preserve totals and clear after area exit; paused combat blocks attack\n";
        std::cout << "[PASS] Nearby drop visibility updates on area exit and suppresses unchanged lists\n";
        std::cout << "[PASS] Monster/drop 16m boundary repeats preserve inclusion, exit and unchanged-list suppression\n";
    }
    // 같은 주변 목록을 보는 수신자는 분할 패킷 구성 결과를 공유한다.
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
        Check(metrics.packets % peers.size() == 0 && metrics.builds <= 10 && metrics.builds > 0 && metrics.failures == 0,
            "Monster packet rebuilt per recipient or cadence changed");
        FieldMonsterPartsHeader snapshot; memcpy(&snapshot, peers.front()->packets.back().data() + sizeof(PacketHeader), sizeof(snapshot));
        Check(snapshot.total <= count && snapshot.count <= FIELD_MONSTERS_PER_PART && snapshot.parts == (std::max)(1, (snapshot.total + 31) / 32) && peers.front()->packets.back().size() <= 1024, "Monster load packet count/size invalid");
        std::cout << "[PASS] Monster load count=" << count << " peers=32 builds=" << metrics.builds << " packets=" << metrics.packets
            << " bytes=" << metrics.bytes << " cpuMs=" << elapsed << '\n';
        for (auto& player : players) load.RemovePlayer(*player);
        load.Tick(200, now + std::chrono::seconds(4));
        Check(load.TakeFieldMonsterMetrics().simulated == 0, "Unoccupied field continued monster simulation");
    }

}
