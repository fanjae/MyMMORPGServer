#pragma once

#include "MapDefinition.h"
#include "MovementSimulation.h"
#include "../Protocol/GamePacket.h"

#include <cstdint>
#include <unordered_map>
#include <memory>
#include <chrono>

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

    uint32_t GetMapId() const { return _definition.mapId; }
    int32_t GetSpawnX() const { return _definition.spawnX; }
    int32_t GetSpawnY() const { return _definition.spawnY; }
    const MapDefinition& GetDefinition() const { return _definition; }

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
    MapDefinition _definition;
    const MapGeometry* _geometry = nullptr;
    std::unique_ptr<MovementSimulation> _simulation;
    uint64_t _tick = 0;
    std::unordered_map<uint32_t, Player*> _players;
    std::unordered_map<uint32_t, Monster*> _monsters;
};
