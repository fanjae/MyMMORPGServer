#pragma once

#include <cstdint>
#include <unordered_map>

class Player;

class Map
{
public:
    explicit Map(uint32_t mapId);

    uint32_t GetMapId() const { return _mapId; }

    bool AddPlayer(Player& player);
    void RemovePlayer(Player& player);
    bool NotifyPlayerEntered(Player& player);
    void NotifyPlayerLeaving(Player& player);
    bool NotifyPlayerMoved(Player& player);

    Player* FindPlayer(uint32_t characterId) const;

private:
    uint32_t _mapId = 0;
    std::unordered_map<uint32_t, Player*> _players;
};
