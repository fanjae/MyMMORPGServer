#include "Map.h"
#include "Player.h"
#include "../ServerCore/Session.h"
#include "../ServerCore/Packet.h"
#include <algorithm>
#include <cstring>

namespace
{
    template<typename T> bool Send3D(Session& session, GamePacketOpcode opcode, const T& payload)
    {
        static_assert(sizeof(PacketHeader) + sizeof(T) <= MAX_PACKET_SIZE);
        PacketHeader header{static_cast<uint16_t>(sizeof(PacketHeader) + sizeof(T)), static_cast<uint16_t>(opcode)};
        char bytes[sizeof(header) + sizeof(payload)];
        memcpy(bytes, &header, sizeof(header)); memcpy(bytes + sizeof(header), &payload, sizeof(payload));
        return session.Send(bytes, sizeof(bytes));
    }
}

bool Map::SendWorld3D(Player& player)
{
    Session* session = player.GetSession();
    if (!Is3D() || session == nullptr) return false;
    auto settings = _world3D->settings; settings.generation = player.GetGeneration();
    if (!Send3D(*session, GamePacketOpcode::World3D, settings)) return false;
    for (auto box : _world3D->boxes)
    {
        box.generation = player.GetGeneration();
        if (!Send3D(*session, GamePacketOpcode::WorldBox3D, box)) return false;
    }
    GeometryEndPacket end{GetMapId(), player.GetGeneration()};
    return Send3D(*session, GamePacketOpcode::WorldEnd3D, end) && SendState3D(player, player);
}

bool Map::SendState3D(Player& recipient, const Player& player)
{
    if (!Is3D() || recipient.GetSession() == nullptr) return false;
    MovementState3DPacket packet{GetMapId(), recipient.GetGeneration(),
        RelayedMovementAction3D{player.GetCharacterId(), player.GetActionTick(), player.GetState3D()}};
    return Send3D(*recipient.GetSession(), GamePacketOpcode::MovementState3D, packet);
}

bool Map::ReceiveActions3D(Player& player, const MovementActionsHeader& header, const MovementAction3D* actions)
{
    ++_movementMetrics.receivedPackets;
    _movementMetrics.receivedBytes += sizeof(PacketHeader) + sizeof(header) + header.count * sizeof(MovementAction3D);
    auto reject = [this]() { ++_movementMetrics.rejectedPackets; return false; };
    if (!Is3D() || player.GetMap() != this || header.mapId != GetMapId() || header.count == 0 ||
        header.count > MAX_MOVEMENT_ACTIONS || header.latestClientTick > UINT64_MAX - 100000) return reject();
    uint64_t sequence = 0, tick = 0;
    for (uint16_t i = 0; i < header.count; ++i)
    {
        const auto& action = actions[i];
        if (action.sequence <= sequence || action.clientTick < tick || action.clientTick > header.latestClientTick ||
            !_world3D->ValidateAction(action)) return reject();
        sequence = action.sequence; tick = action.clientTick;
    }
    if (!player.AcceptActionBatch(header.generation, header.batchSequence, header.latestClientTick)) return reject();
    auto now = std::chrono::steady_clock::now();
    for (uint16_t i = 0; i < header.count; ++i)
    {
        if (!player.AcceptAction3D(actions[i])) { ++_movementMetrics.rejectedActions; continue; }
        ++_movementMetrics.acceptedActions;
        for (const auto& entry : _players)
        {
            auto& queue = _pending3D[entry.first];
            if (queue.size() >= 256)
            {
                ++_movementMetrics.queueOverflows;
                if (entry.second->GetSession() != nullptr) entry.second->GetSession()->RequestClose();
                continue;
            }
            queue.push_back({{player.GetCharacterId(), header.latestClientTick, actions[i]}, now});
            _movementMetrics.peakPendingPerRecipient = (std::max)(_movementMetrics.peakPendingPerRecipient, queue.size());
        }
    }
    return true;
}

void Map::Tick3D(uint64_t tick, std::chrono::steady_clock::time_point now)
{
    constexpr size_t maxCount = (MAX_PACKET_SIZE - sizeof(PacketHeader) - sizeof(MovementActionsBroadcastHeader)) / sizeof(RelayedMovementAction3D);
    for (const auto& entry : _players)
    {
        Player& recipient = *entry.second;
        auto found = _pending3D.find(entry.first);
        if (found == _pending3D.end() || found->second.empty() || !recipient.CanSendMovement(now)) continue;
        auto& queue = found->second;
        MovementActionsBroadcastHeader payload{GetMapId(), recipient.GetGeneration(), tick, static_cast<uint16_t>((std::min)(queue.size(), maxCount))};
        PacketHeader header{static_cast<uint16_t>(sizeof(PacketHeader) + sizeof(payload) + payload.count * sizeof(RelayedMovementAction3D)),
            static_cast<uint16_t>(GamePacketOpcode::MovementActionsBroadcast3D)};
        std::vector<char> bytes(header.size);
        memcpy(bytes.data(), &header, sizeof(header)); memcpy(bytes.data() + sizeof(header), &payload, sizeof(payload));
        for (uint16_t i = 0; i < payload.count; ++i)
        {
            auto pending = queue.front(); queue.pop_front();
            auto waited = (std::max)(int64_t{0}, static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - pending.receivedAt).count()));
            // tick이 밀린 경우에도 실제 서버 대기 시간을 사용하며 네트워크 지연은 추정하지 않는다.
            pending.relay.latestClientTick += static_cast<uint64_t>(waited / 20);
            _movementMetrics.maxRelayWaitMs = (std::max)(_movementMetrics.maxRelayWaitMs, waited);
            memcpy(bytes.data() + sizeof(header) + sizeof(payload) + i * sizeof(pending.relay), &pending.relay, sizeof(pending.relay));
        }
        recipient.MarkMovementSent(now);
        Session* session = recipient.GetSession();
        if (session == nullptr || !session->Send(bytes.data(), static_cast<int32_t>(bytes.size())))
        {
            ++_movementMetrics.sendFailures;
            if (session != nullptr) session->RequestClose();
        }
        else
        {
            ++_movementMetrics.relayPackets; _movementMetrics.relayBytes += bytes.size(); _movementMetrics.relayActions += payload.count;
        }
    }
}
