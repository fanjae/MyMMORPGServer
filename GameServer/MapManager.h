#pragma once

#include "Map.h"
#include "MapGeometry.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <string>

class MapManager
{
public:
    ~MapManager();

    bool LoadMaps(const std::string& path);
    bool LoadGeometry(const std::string& directory);
    Map* FindMap(uint32_t mapId) const;
    const MapGeometry* FindGeometry(uint32_t mapId) const;
    void Advance();
    uint32_t GetWaitMilliseconds() const;

private:
    std::unordered_map<uint32_t, std::unique_ptr<Map>> _maps;
    std::unordered_map<uint32_t, MapGeometry> _geometries;
    std::chrono::steady_clock::time_point _nextTick = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
    uint64_t _tick = 0;
};
