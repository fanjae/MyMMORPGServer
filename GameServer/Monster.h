#pragma once

#include <cstdint>

class Map;

class Monster
{
public:
    Monster(uint32_t monsterId, int32_t x, int32_t y);

    uint32_t GetMonsterId() const { return _monsterId; }
    int32_t GetX() const { return _x; }
    int32_t GetY() const { return _y; }

    Map* GetMap() const { return _map; }
    void SetMap(Map* map) { _map = map; }

private:
    uint32_t _monsterId = 0;
    int32_t _x = 0;
    int32_t _y = 0;
    Map* _map = nullptr;
};
