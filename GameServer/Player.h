#pragma once

#include <cstdint>
#include <string>

class Map;

class Player
{
public:
    Player(uint32_t characterId, uint32_t accountId, std::string name, uint16_t level);

    uint32_t GetCharacterId() const { return _characterId; }
    uint32_t GetAccountId() const { return _accountId; }
    const std::string& GetName() const { return _name; }
    uint16_t GetLevel() const { return _level; }

    Map* GetMap() const { return _map; }
    void SetMap(Map* map) { _map = map; }

private:
    uint32_t _characterId = 0;
    uint32_t _accountId = 0;
    std::string _name;
    uint16_t _level = 0;
    Map* _map = nullptr;
};