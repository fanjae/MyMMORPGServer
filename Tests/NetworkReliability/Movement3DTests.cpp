#include "../../GameServer/Map.h"
#include "../../GameServer/Player.h"
#include "../../GameServer/World3D.h"
#include "../../ServerCore/Session.h"
#include "../../ServerCore/SocketUtils.h"
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
    class Recipient : public Session
    {
    public:
        Recipient() : Session(SocketUtils::CreateSocket()) {}
        bool fail = false;
        std::vector<std::vector<char>> packets;
        bool Send(const char* bytes, int32_t size) override { if (fail) return false; packets.emplace_back(bytes, bytes + size); return true; }
    protected:
        bool OnPacket(uint16_t, const char*, uint16_t) override { return true; }
    };
    struct Fixture
    {
        std::unordered_map<uint32_t, World3DDefinition> worlds;
        Map map{MapDefinition{100000002, 0, 0, -400, 400, -200, 400, 80, 12}};
        Player a{1001, 1, "owner3d", 1}, b{2001, 2, "observer3d", 1};
        Recipient sa, sb;
        Fixture()
        {
            std::string error;
            std::unordered_map<uint32_t, MapDefinition> definitions{{100000002, map.GetDefinition()},
                {100000003, {100000003, -200, 0, -400, 400, -200, 400, 80, 12}}};
            // 소스 파일 위치를 기준으로 찾아 실행 작업 디렉터리에 의존하지 않는다.
            auto data = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "data";
            Check(World3DLoader::Load(data.string(), definitions, worlds, error), error.c_str());
            map.SetWorld3D(worlds.at(100000002)); a.SetSession(&sa); b.SetSession(&sb);
            Check(map.AddPlayer(a) && map.AddPlayer(b), "3D registration failed");
        }
        MovementAction3D Action(uint64_t sequence, MovementActionKind kind, uint64_t jumpId = 1)
        {
            MovementAction3D action = a.GetState3D(); action.sequence = action.clientTick = sequence;
            action.jumpId = jumpId; action.kind = kind;
            action.grounded = kind != MovementActionKind::Jump && kind != MovementActionKind::Fall;
            action.supportId = action.grounded ? 1 : 0;
            action.y = action.grounded ? 0 : 3040; action.velocityY = action.grounded ? 0 : 152000;
            return action;
        }
        bool Send(uint64_t batch, const std::vector<MovementAction3D>& actions)
        {
            MovementActionsHeader header{map.GetMapId(), a.GetGeneration(), batch, actions.back().clientTick, static_cast<uint16_t>(actions.size())};
            return map.ReceiveActions3D(a, header, actions.data());
        }
    };
}
void RunMovement3DTests()
{
    auto run = [](const char* name, const std::function<void()>& test) { test(); std::cout << "[PASS] " << name << '\n'; };
    run("3D world loads both maps and bootstrap packets retain XYZ support and generation", []
    {
        Fixture f;
        Check(f.worlds.size() == 2 && f.worlds.at(100000002).SpawnSupport() == 1, "3D world data missing");
        Check(f.map.SendWorld3D(f.a), "3D bootstrap failed");
        Check(f.sa.packets.size() == 8, "3D world/boxes/end/state ordering changed");
        PacketHeader header; memcpy(&header, f.sa.packets.front().data(), sizeof(header));
        Check(header.opcode == static_cast<uint16_t>(GamePacketOpcode::World3D) && header.size == 82, "3D world wire size changed");
        Check(f.map.MovePlayer(f.a, 1, 1) == MoveResult::WrongMovementMode, "3D accepted legacy absolute movement");
    });
    run("3D server validates XYZ and diagonal speed before mutating a batch", []
    {
        Fixture f;
        auto valid = f.Action(1, MovementActionKind::Checkpoint, 0); valid.z = 10000; valid.yaw = 9000;
        Check(f.Send(1, {valid}) && f.a.GetState3D().z == 10000, "Valid Z movement rejected");
        auto invalid = f.Action(3, MovementActionKind::Checkpoint, 0); invalid.z = INT64_MAX;
        auto jump = f.Action(2, MovementActionKind::Jump);
        Check(!f.Send(2, {jump, invalid}) && !f.a.IsAirborne(), "Invalid Z partially applied");
        invalid = valid; invalid.sequence = invalid.clientTick = 2; invalid.velocityX = invalid.velocityZ = 80000;
        Check(!f.Send(2, {invalid}), "Diagonal speed bypassed magnitude limit");
        invalid.velocityX = invalid.velocityZ = 0; invalid.supportId = 99;
        Check(!f.Send(2, {invalid}), "Unknown support accepted");
    });
    run("3D matching landing unlocks jump and grounded checkpoints cannot bypass airborne state", []
    {
        Fixture f;
        Check(f.Send(1, {f.Action(1, MovementActionKind::Jump)}) && f.a.IsAirborne(), "3D jump rejected");
        Check(f.Send(2, {f.Action(2, MovementActionKind::Jump, 2)}) && f.a.GetJumpId() == 1, "3D airborne rejump accepted");
        Check(f.Send(3, {f.Action(3, MovementActionKind::Checkpoint)}) && f.a.IsAirborne(), "3D checkpoint unlocked landing");
        Check(f.Send(4, {f.Action(4, MovementActionKind::Land, 99)}) && f.a.IsAirborne(), "Wrong 3D jumpId unlocked landing");
        Check(f.Send(5, {f.Action(5, MovementActionKind::Land)}) && !f.a.IsAirborne(), "3D landing rejected");
        Check(f.Send(6, {f.Action(6, MovementActionKind::Jump)}) && !f.a.IsAirborne(), "Old 3D jump replayed");
        Check(f.Send(7, {f.Action(7, MovementActionKind::Jump, 2)}) && f.a.IsAirborne(), "New 3D jump rejected");
    });
    run("3D server never runs physics and recipient fan-out stays bounded at 42 records", []
    {
        Fixture f;
        for (uint64_t i = 1; i <= 80; ++i) Check(f.Send(i, {f.Action(i, MovementActionKind::Checkpoint, 0)}), "Checkpoint rejected");
        auto now = std::chrono::steady_clock::now();
        for (uint64_t tick = 0; tick < 50; ++tick) f.map.Tick(tick, now + std::chrono::milliseconds(tick * 20));
        Check(f.sb.packets.size() == 2 && f.a.GetState3D().y == 0, "3D server emitted unsolicited states or simulated physics");
        for (const auto& bytes : f.sb.packets) Check(bytes.size() <= MAX_PACKET_SIZE, "3D relay exceeded packet limit");
        MovementActionsBroadcastHeader header;
        memcpy(&header, f.sb.packets.front().data() + sizeof(PacketHeader), sizeof(header));
        Check(header.count == 42 && f.sb.packets.front().size() == 4058, "3D packed relay limit changed");
        auto metrics = f.map.TakeMovementMetrics();
        Check(metrics.receivedBytes == 80 * 118 && metrics.relayPackets == 4 && metrics.relayActions == 160 && metrics.pendingActions == 0, "3D metrics mismatch");
    });
    run("3D reentry rejects old generation and send failure is isolated", []
    {
        Fixture f;
        auto action = f.Action(1, MovementActionKind::Jump);
        MovementActionsHeader old{f.map.GetMapId(), f.a.GetGeneration(), 1, 1, 1};
        f.map.RemovePlayer(f.a); Check(f.map.AddPlayer(f.a), "3D reentry failed");
        Check(!f.map.ReceiveActions3D(f.a, old, &action) && !f.a.IsAirborne(), "3D old generation mutated state");
        Check(f.Send(1, {action}), "3D current generation rejected"); f.sa.fail = true;
        f.map.Tick(1, std::chrono::steady_clock::now());
        Check(f.sa.IsCloseRequested() && f.sb.packets.size() == 1, "3D failure affected observer");
        f.map.RemovePlayer(f.a); Check(f.map.TakeMovementMetrics().pendingActions == 0, "3D departure left actions");
    });
}
