#pragma once

#include <cstdint>
#include <unordered_map>

class Player;

class PlayerManager
{
public:
    bool Add(Player& player);
    void Remove(Player& player);

    Player* FindByCharacterId(uint32_t characterId) const;
    Player* FindByAccountId(uint32_t accountId) const;

private:
    std::unordered_map<uint32_t, Player*> _playersByCharacterId;
    std::unordered_map<uint32_t, Player*> _playersByAccountId;
};
