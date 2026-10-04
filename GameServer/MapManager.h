#pragma once

#include "Map.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <string>

class MapManager
{
public:
    ~MapManager();

    bool LoadMaps(const std::string& path);
    Map* FindMap(uint32_t mapId) const;

private:
    std::unordered_map<uint32_t, std::unique_ptr<Map>> _maps;
};
