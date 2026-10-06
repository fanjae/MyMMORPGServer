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
    bool AcceptInput(const MovementInputPacket& input);
    uint64_t GetGeneration() const { return _generation; }
    uint64_t GetInputSequence() const { return _appliedInputSequence; }
    PlatformMovementState& GetPlatformState() { return _platformState; }
    const PlatformMovementState& GetPlatformState() const { return _platformState; }
    PlatformMovementInput ConsumeInput(std::chrono::steady_clock::time_point now, bool& expired);
    void FinishInput() { _platformState.jumpHeld = _input.jumpHeld; }

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
    uint64_t _inputSequence = 0;
    uint64_t _appliedInputSequence = 0;
    PlatformMovementState _platformState;
    PlatformMovementInput _input;
    bool _jumpPending = false;
    std::chrono::steady_clock::time_point _inputTime;
};
