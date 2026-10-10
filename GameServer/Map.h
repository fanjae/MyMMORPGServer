#pragma once

#include "MapDefinition.h"
#include "MovementSimulation.h"
#include "../Protocol/GamePacket.h"
#include "World3D.h"

#include <cstdint>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <deque>

class Monster;
class Player;

class Map
{
public:
    explicit Map(const MapDefinition& definition);
    void SetGeometry(const MapGeometry& geometry);
    bool IsPlatformer() const;
    bool SendGeometry(Player& player);
    bool SendMovementState(Player& recipient, const Player& player, MovementStateReason reason = MovementStateReason::Normal);
    void Tick(uint64_t tick, std::chrono::steady_clock::time_point now);
    bool ReceiveMovementActions(Player& player, const MovementActionsHeader& header, const MovementAction* actions);
    bool Is3D() const { return _world3D != nullptr; }
    void SetWorld3D(const World3DDefinition& world) { _world3D = &world; }
    bool SendWorld3D(Player& player);
    bool SendState3D(Player& recipient, const Player& player);
    bool ReceiveActions3D(Player& player, const MovementActionsHeader& header, const MovementAction3D* actions);

    struct MovementMetrics
    {
        uint64_t receivedPackets = 0, receivedBytes = 0, rejectedPackets = 0;
        uint64_t acceptedActions = 0, rejectedActions = 0;
        uint64_t relayPackets = 0, relayBytes = 0, relayActions = 0, sendFailures = 0, queueOverflows = 0;
        size_t pendingActions = 0, maxPendingPerRecipient = 0, peakPendingPerRecipient = 0;
        int64_t oldestPendingMs = 0, maxRelayWaitMs = 0;
    };
    MovementMetrics TakeMovementMetrics();

    uint32_t GetMapId() const { return _definition.mapId; }
    int32_t GetSpawnX() const { return _definition.spawnX; }
    int32_t GetSpawnY() const { return _definition.spawnY; }
    const MapDefinition& GetDefinition() const { return _definition; }
    size_t GetPlayerCount() const { return _players.size(); }

    bool AddPlayer(Player& player);
    void RemovePlayer(Player& player);
    MoveResult MovePlayer(Player& player, int32_t x, int32_t y);
    bool AddMonster(Monster& monster);
    void RemoveMonster(Monster& monster);

    bool NotifyPlayerEntered(Player& player);
    bool SendMapInfo(Player& player);
    void NotifyPlayerLeaving(Player& player);
    bool NotifyPlayerMoved(Player& player);
    bool NotifyPlayerChat(Player& player, const char* message);

    Player* FindPlayer(uint32_t characterId) const;
    Monster* FindMonster(uint32_t monsterId) const;

private:
    void Tick3D(uint64_t tick, std::chrono::steady_clock::time_point now);
    const World3DDefinition* _world3D = nullptr;
    struct Pending3D { RelayedMovementAction3D relay; std::chrono::steady_clock::time_point receivedAt; };
    std::unordered_map<uint32_t, std::deque<Pending3D>> _pending3D;
    MapDefinition _definition;
    const MapGeometry* _geometry = nullptr;
    struct PendingAction { RelayedMovementAction relay; uint64_t receivedTick; std::chrono::steady_clock::time_point receivedAt; };
    std::unordered_map<uint32_t, std::deque<PendingAction>> _pendingActions;
    MovementMetrics _movementMetrics;
    uint64_t _tick = 0;
    std::unordered_map<uint32_t, Player*> _players;
    std::unordered_map<uint32_t, Monster*> _monsters;
};
