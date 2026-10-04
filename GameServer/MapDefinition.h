#pragma once

#include <cstdint>

struct MapDefinition
{
    uint32_t mapId = 0;
    int32_t spawnX = 0;
    int32_t spawnY = 0;
    int32_t minX = 0;
    int32_t maxX = 0;
    int32_t minY = 0;
    int32_t maxY = 0;
    uint32_t moveSpeed = 0;
    uint32_t moveBurst = 0;

    bool Contains(int32_t x, int32_t y) const
    {
        return x >= minX && x <= maxX && y >= minY && y <= maxY;
    }

    bool IsValid() const
    {
        return mapId != 0 && minX < maxX && minY < maxY && Contains(spawnX, spawnY) &&
            moveSpeed > 0 && moveSpeed <= 10000 && moveBurst > 0 && moveBurst <= moveSpeed;
    }
};
