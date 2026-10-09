#include "../../GameServer/Player.h"
#include "../../GameServer/Map.h"
#include "../../ServerCore/Session.h"
#include "../../ServerCore/SocketUtils.h"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>

namespace
{
    void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
    class ActionSession : public Session
    {
    public:
        ActionSession() : Session(SocketUtils::CreateSocket()) {}
        bool Send(const char* data, int32_t size) override
        {
            if (fail) return false;
            packets.emplace_back(data, data + size); return true;
        }
        bool fail = false;
        std::vector<std::vector<char>> packets;
    protected:
        bool OnPacket(uint16_t, const char*, uint16_t) override { return true; }
    };
    struct Fixture
    {
        MapGeometry geometry;
        Map map{MapDefinition{100000000, 0, 0, -400, 400, -200, 200, 80, 12}};
        Player a{1001, 1, "owner", 1}, b{2001, 2, "observer", 1};
        ActionSession sa, sb;
        Fixture()
        {
            geometry.map = map.GetDefinition();
            geometry.movement = {1, MovementMode::Platformer, 1, 6, 6, 80, 240, 480, 320};
            geometry.footholds.push_back({1, -400, -6, 400, -6, 0, 0});
            map.SetGeometry(geometry); a.SetSession(&sa); b.SetSession(&sb);
            Check(map.AddPlayer(a) && map.AddPlayer(b), "Fixture registration failed");
        }
        MovementAction Action(uint64_t sequence, MovementActionKind kind, uint64_t jumpId = 1)
        {
            MovementAction action;
            action.sequence = sequence; action.clientTick = sequence; action.jumpId = jumpId; action.kind = kind;
            action.grounded = kind == MovementActionKind::Land || kind == MovementActionKind::Respawn;
            action.footholdId = action.grounded ? 1 : 0;
            action.y = action.grounded ? 0 : 10000;
            action.velocityY = action.grounded ? 0 : 200000;
            return action;
        }
        bool Send(uint64_t batch, const std::vector<MovementAction>& actions)
        {
            MovementActionsHeader header{map.GetMapId(), a.GetGeneration(), batch, actions.back().clientTick, static_cast<uint16_t>(actions.size())};
            return map.ReceiveMovementActions(a, header, actions.data());
        }
    };
}

void RunMovementAcknowledgementTests()
{
    auto run = [](const char* name, const std::function<void()>& test) { test(); std::cout << "[PASS] " << name << '\n'; };
    run("Client landing exclusively unlocks the server jump state", []
    {
        Fixture f;
        Check(f.Send(1, {f.Action(1, MovementActionKind::Jump)}) && f.a.IsAirborne(), "Jump did not lock");
        Check(f.Send(2, {f.Action(2, MovementActionKind::Jump, 2)}) && f.a.GetJumpId() == 1, "Repeated jump accepted");
        auto checkpoint = f.Action(3, MovementActionKind::Land); checkpoint.kind = MovementActionKind::Checkpoint;
        Check(f.Send(3, {checkpoint}) && f.a.IsAirborne(), "Grounded checkpoint bypassed landing");
        Check(f.Send(4, {f.Action(4, MovementActionKind::Land, 99)}) && f.a.IsAirborne(), "Wrong landing identity unlocked");
        Check(f.Send(5, {f.Action(5, MovementActionKind::Land)}) && !f.a.IsAirborne(), "Owner landing did not unlock");
        Check(f.Send(6, {f.Action(6, MovementActionKind::Jump)}) && !f.a.IsAirborne(), "Old jump resurrected after landing");
        Check(f.Send(7, {f.Action(7, MovementActionKind::Jump, 2)}) && f.a.IsAirborne() && f.a.GetJumpId() == 2, "Fresh jump rejected");
    });
    run("Server ticks never integrate movement, gravity, landing or input expiry", []
    {
        Fixture f;
        auto jump = f.Action(1, MovementActionKind::Jump); jump.x = 5000; jump.velocityX = 80000; jump.horizontal = 1;
        Check(f.Send(1, {jump}), "Jump rejected");
        auto start = std::chrono::steady_clock::time_point{};
        for (uint64_t i = 1; i <= 250; ++i) f.map.Tick(i, start + std::chrono::milliseconds(i * 20));
        Check(f.a.GetPlatformState().x == 5 && f.a.GetPlatformState().y == 10 && f.a.IsAirborne(), "Server simulated movement or timeout landing");
        Check(f.sb.packets.size() == 1, "Idle server generated periodic movement traffic");
    });
    run("Broadcast bundles both clients and preserves jump then landing under five sends per second", []
    {
        Fixture f;
        auto now = std::chrono::steady_clock::time_point{};
        Check(f.Send(1, {f.Action(1, MovementActionKind::Jump), f.Action(2, MovementActionKind::Land)}), "Ordered batch rejected");
        for (uint64_t i = 0; i < 50; ++i)
        {
            MovementAction checkpoint = f.Action(i + 3, MovementActionKind::Land); checkpoint.kind = MovementActionKind::Checkpoint;
            Check(f.Send(i + 2, {checkpoint}), "Checkpoint rejected");
            f.map.Tick(i, now + std::chrono::milliseconds(i * 20));
        }
        Check(f.sa.packets.size() == 5 && f.sb.packets.size() == 5, "Relay budget violated");
        const auto& bytes = f.sb.packets.front();
        MovementActionsBroadcastHeader header;
        memcpy(&header, bytes.data() + sizeof(PacketHeader), sizeof(header));
        Check(header.count == 3 && bytes.size() == 4 + 22 + 3 * 75, "Relay layout/count mismatch");
        RelayedMovementAction first, second;
        memcpy(&first, bytes.data() + 4 + 22, sizeof(first)); memcpy(&second, bytes.data() + 4 + 22 + 75, sizeof(second));
        Check(first.characterId == 1001 && first.action.kind == MovementActionKind::Jump && second.action.kind == MovementActionKind::Land, "Ordered events lost");
    });
    run("Malformed batch is atomic and previous map generation cannot update state", []
    {
        Fixture f;
        auto invalid = f.Action(2, MovementActionKind::Land); invalid.footholdId = 99;
        Check(!f.Send(1, {f.Action(1, MovementActionKind::Jump), invalid}) && !f.a.IsAirborne(), "Malformed batch partially applied");
        auto jump = f.Action(1, MovementActionKind::Jump);
        MovementActionsHeader old{f.map.GetMapId(), f.a.GetGeneration(), 1, 1, 1};
        f.map.RemovePlayer(f.a); Check(f.map.AddPlayer(f.a), "Reentry failed");
        Check(!f.map.ReceiveMovementActions(f.a, old, &jump) && !f.a.IsAirborne(), "Previous generation accepted");
        Check(f.Send(1, {jump}) && !f.Send(1, {jump}), "Duplicate batch accepted");
    });
    run("Relay size remains bounded and failed recipients do not block observers", []
    {
        Fixture f; f.sa.fail = true;
        for (uint64_t i = 1; i <= 80; ++i)
        {
            auto action = f.Action(i, MovementActionKind::Land, 0); action.kind = MovementActionKind::Checkpoint;
            Check(f.Send(i, {action}), "Checkpoint rejected");
        }
        auto now = std::chrono::steady_clock::time_point{};
        f.map.Tick(1, now); f.map.Tick(11, now + std::chrono::milliseconds(200));
        Check(f.sa.IsCloseRequested() && f.sb.packets.size() == 2, "Recipient failure was not isolated");
        for (const auto& packet : f.sb.packets) Check(packet.size() <= MAX_PACKET_SIZE, "Oversized broadcast");
    });
    run("Movement metrics separate batch rejection, action rejection and recipient fan-out", []
    {
        Fixture f;
        Check(f.Send(1, {f.Action(1, MovementActionKind::Jump)}), "Initial jump rejected");
        Check(f.Send(2, {f.Action(2, MovementActionKind::Jump, 2)}), "Airborne batch rejected");
        auto invalid = f.Action(3, MovementActionKind::Land); invalid.footholdId = 99;
        Check(!f.Send(3, {invalid}), "Invalid foothold accepted");
        auto received = f.map.TakeMovementMetrics();
        Check(received.receivedPackets == 3 && received.receivedBytes == 3 * (4 + 30 + 63), "Received wire bytes mismatch");
        Check(received.rejectedPackets == 1 && received.acceptedActions == 1 && received.rejectedActions == 1,
            "Batch and action rejection were conflated");
        Check(received.pendingActions == 2 && received.peakPendingPerRecipient == 1, "Recipient fan-out was not counted");
        f.map.Tick(1, std::chrono::steady_clock::now() + std::chrono::milliseconds(250));
        auto sent = f.map.TakeMovementMetrics();
        Check(sent.receivedPackets == 0 && sent.relayPackets == 2 && sent.relayActions == 2 && sent.relayBytes == 2 * (4 + 22 + 75),
            "Interval reset or relay bytes mismatch");
        Check(sent.pendingActions == 0 && sent.maxRelayWaitMs >= 250, "Queue did not drain or wait age was not measured");
    });
    run("Metrics reads preserve pending queues and map departure removes queued recipient copies", []
    {
        Fixture f;
        Check(f.Send(1, {f.Action(1, MovementActionKind::Jump), f.Action(2, MovementActionKind::Land)}), "Ordered batch rejected");
        auto first = f.map.TakeMovementMetrics(), second = f.map.TakeMovementMetrics();
        Check(first.pendingActions == 4 && second.pendingActions == 4 && second.receivedPackets == 0 &&
            second.maxPendingPerRecipient == 2 && second.peakPendingPerRecipient == 2, "Metrics read mutated pending actions");
        f.map.RemovePlayer(f.a);
        Check(f.map.TakeMovementMetrics().pendingActions == 0, "Departure left queued actions for the old map presence");
    });
    run("Failed sends and queue overflow are excluded from accepted relay traffic", []
    {
        Fixture f;
        for (uint64_t i = 1; i <= 257; ++i)
        {
            auto action = f.Action(i, MovementActionKind::Land, 0); action.kind = MovementActionKind::Checkpoint;
            Check(f.Send(i, {action}), "Checkpoint rejected");
        }
        auto queued = f.map.TakeMovementMetrics();
        Check(queued.pendingActions == 512 && queued.peakPendingPerRecipient == 256 && queued.queueOverflows == 2,
            "Queue overflow did not retain the bounded recipient copies");
        f.sa.fail = true;
        f.map.Tick(1, std::chrono::steady_clock::now());
        auto sent = f.map.TakeMovementMetrics();
        Check(sent.sendFailures == 1 && sent.relayPackets == 1 && sent.relayActions == 54 && sent.relayBytes == 4076 &&
            sent.pendingActions == 404, "Failed recipient was counted as successful traffic");
    });
    run("Local fan-out measurement scales across 2, 8 and 32 recipients", []
    {
        for (uint32_t count : {2u, 8u, 32u})
        {
            Fixture f;
            std::vector<std::unique_ptr<ActionSession>> sessions;
            std::vector<std::unique_ptr<Player>> players;
            for (uint32_t i = 2; i < count; ++i)
            {
                sessions.push_back(std::make_unique<ActionSession>());
                players.push_back(std::make_unique<Player>(3000 + i, 3000 + i, "load", static_cast<uint16_t>(1)));
                players.back()->SetSession(sessions.back().get());
                Check(f.map.AddPlayer(*players.back()), "Load recipient registration failed");
            }
            for (uint64_t i = 1; i <= 32; ++i)
            {
                auto action = f.Action(i, MovementActionKind::Land, 0); action.kind = MovementActionKind::Checkpoint;
                Check(f.Send(i, {action}), "Load checkpoint rejected");
            }
            auto queued = f.map.TakeMovementMetrics();
            f.map.Tick(1, std::chrono::steady_clock::now());
            auto sent = f.map.TakeMovementMetrics();
            Check(queued.pendingActions == count * 32 && sent.relayPackets == count && sent.relayActions == count * 32 &&
                sent.relayBytes == count * (4 + 22 + 32 * 75) && sent.pendingActions == 0, "Fan-out accounting mismatch");
            std::cout << "[METRICS] local recipients=" << count << " receivedPackets=" << queued.receivedPackets
                << " receivedBytes=" << queued.receivedBytes << " pendingActions=" << queued.pendingActions
                << " relayPackets=" << sent.relayPackets << " relayBytes=" << sent.relayBytes << '\n';
            for (const auto& player : players) f.map.RemovePlayer(*player);
        }
    });
}
