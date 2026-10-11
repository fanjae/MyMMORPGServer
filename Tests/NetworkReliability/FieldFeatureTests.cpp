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
}
