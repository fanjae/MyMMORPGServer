#include "MapManager.h"
#include "Map.h"

MapManager::~MapManager() = default;

Map& MapManager::CreateMap(uint32_t mapId, int32_t spawnX, int32_t spawnY)
{
    auto it = _maps.find(mapId);

    if (it != _maps.end())
        return *it->second;

    auto map = std::make_unique<Map>(mapId, spawnX, spawnY);
    Map& result = *map;

    _maps.emplace(mapId, std::move(map));
    return result;
}

Map* MapManager::FindMap(uint32_t mapId) const
{
    auto it = _maps.find(mapId);

    if (it == _maps.end())
        return nullptr;

    return it->second.get();
}
