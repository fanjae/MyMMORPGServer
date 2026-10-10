#pragma once
#include "../Protocol/GamePacket.h"
#include "MapDefinition.h"
#include <vector>
#include <unordered_map>
#include <string>

struct World3DDefinition
{
    World3DPacket settings;
    std::vector<WorldBox3DPacket> boxes;
    uint32_t SpawnSupport() const;
    bool ValidateAction(const MovementAction3D& action) const;
};

class World3DLoader
{
public:
    static bool Load(const std::string& directory, const std::unordered_map<uint32_t, MapDefinition>& maps,
        std::unordered_map<uint32_t, World3DDefinition>& worlds, std::string& error);
};
