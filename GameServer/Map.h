#pragma once

#include "MapDefinition.h"
#include "../Protocol/GamePacket.h"

#include <cstdint>
#include <unordered_map>

class Monster;
class Player;

class Map
{
public:
    explicit Map(const MapDefinition& definition);

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
    std::unordered_map<uint32_t, Player*> _players;
    std::unordered_map<uint32_t, Monster*> _monsters;
};
