#pragma once

#include "Map.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

class MapManager
{
public:
    ~MapManager();

    Map& CreateMap(uint32_t mapId, int32_t spawnX, int32_t spawnY);
    Map* FindMap(uint32_t mapId) const;

private:
    std::unordered_map<uint32_t, std::unique_ptr<Map>> _maps;
};
