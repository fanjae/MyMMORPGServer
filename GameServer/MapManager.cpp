#include "MapManager.h"
#include "Map.h"

MapManager::~MapManager() = default;

Map& MapManager::CreateMap(uint32_t mapId)
{
    auto it = _maps.find(mapId);

    if (it != _maps.end())
        return *it->second;

    auto map = std::make_unique<Map>(mapId);
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
