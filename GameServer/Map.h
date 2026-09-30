#pragma once

#include <cstdint>
#include <unordered_map>

class Monster;
class Player;

class Map
{
public:
    Map(uint32_t mapId, int32_t spawnX, int32_t spawnY);

    uint32_t GetMapId() const { return _mapId; }
    int32_t GetSpawnX() const { return _spawnX; }
    int32_t GetSpawnY() const { return _spawnY; }

    bool AddPlayer(Player& player);
    void RemovePlayer(Player& player);
    bool MovePlayer(Player& player, int32_t x, int32_t y);
    bool AddMonster(Monster& monster);
    void RemoveMonster(Monster& monster);

    bool NotifyPlayerEntered(Player& player);
    void NotifyPlayerLeaving(Player& player);
    bool NotifyPlayerMoved(Player& player);
    bool NotifyPlayerChat(Player& player, const char* message);

    Player* FindPlayer(uint32_t characterId) const;
    Monster* FindMonster(uint32_t monsterId) const;

private:
    uint32_t _mapId = 0;
    int32_t _spawnX = 0;
    int32_t _spawnY = 0;
    std::unordered_map<uint32_t, Player*> _players;
    std::unordered_map<uint32_t, Monster*> _monsters;
};
