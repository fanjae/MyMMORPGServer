#pragma once

#include "MovementValidator.h"
#include "MovementSimulation.h"
#include "../Protocol/GamePacket.h"
#include <chrono>

#include <cstdint>
#include <string>

class Session;
class Map;

class Player
{
public:
    Player(uint32_t characterId, uint32_t accountId, std::string name, uint16_t level);

    uint32_t GetCharacterId() const { return _characterId; }
    uint32_t GetAccountId() const { return _accountId; }
    const std::string& GetName() const { return _name; }
    uint16_t GetLevel() const { return _level; }
    int32_t GetX() const { return _x; }
    int32_t GetY() const { return _y; }
    void SetPosition(int32_t x, int32_t y) { _x = x; _y = y; }

    Session* GetSession() const { return _session; }
    void SetSession(Session* session) { _session = session; }

    Map* GetMap() const { return _map; }
    void SetMap(Map* map) { _map = map; }
    MovementValidator& GetMovementValidator() { return _movementValidator; }
    bool AcceptMoveSequence(uint64_t sequence);
    void BeginMap();
    bool AcceptAction(const MovementAction& action);
    bool AcceptAction3D(const MovementAction3D& action);
    const MovementAction3D& GetState3D() const { return _state3D; }
    void SetState3D(const MovementAction3D& state) { _state3D = state; }
    bool AcceptActionBatch(uint64_t generation, uint64_t sequence, uint64_t latestTick);
    uint64_t GetGeneration() const { return _generation; }
    uint64_t GetActionSequence() const { return _acceptedActionSequence; }
    uint64_t GetJumpId() const { return _jumpId; }
    bool IsAirborne() const { return _airborne; }
    uint64_t GetActionTick() const { return _actionTick; }
    bool CanSendMovement(std::chrono::steady_clock::time_point now) const { return now >= _nextMovementSend; }
    void MarkMovementSent(std::chrono::steady_clock::time_point now) { _nextMovementSend = now + std::chrono::milliseconds(200); }
    PlatformMovementState& GetPlatformState() { return _platformState; }
    const PlatformMovementState& GetPlatformState() const { return _platformState; }

private:
    uint32_t _characterId = 0;
    uint32_t _accountId = 0;
    std::string _name;
    uint16_t _level = 0;
    int32_t _x = 0;
    int32_t _y = 0;
    Session* _session = nullptr;
    Map* _map = nullptr;
    MovementValidator _movementValidator;
    uint64_t _lastMoveSequence = 0;
    uint64_t _generation = 0;
    uint64_t _actionSequence = 0;
    uint64_t _acceptedActionSequence = 0;
    uint64_t _batchSequence = 0;
    uint64_t _latestClientTick = 0;
    uint64_t _actionTick = 0;
    uint64_t _jumpId = 0;
    bool _airborne = false;
    std::chrono::steady_clock::time_point _nextMovementSend{};
    PlatformMovementState _platformState;
    MovementAction3D _state3D;
};
