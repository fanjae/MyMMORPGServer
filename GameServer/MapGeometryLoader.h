#pragma once

#include "MapGeometry.h"

#include <string>
#include <unordered_map>

class MapGeometryLoader
{
public:
    static bool Load(const std::string& directory, const std::unordered_map<uint32_t, MapDefinition>& maps, std::unordered_map<uint32_t, MapGeometry>& geometries, std::string& error);
    static bool Validate(const MapGeometry& geometry, std::string& error);
};
