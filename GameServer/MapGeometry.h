#pragma once

#include "MapDefinition.h"

#include <vector>

enum class MovementMode : uint8_t
{
    Free = 0,
    Platformer = 1
};

struct FootholdDefinition
{
    uint32_t footholdId = 0;
    int32_t x1 = 0;
    int32_t y1 = 0;
    int32_t x2 = 0;
    int32_t y2 = 0;
    uint32_t prevId = 0;
    uint32_t nextId = 0;
};

struct MapCollisionDefinition
{
    uint32_t colliderId = 0;
    int32_t minX = 0;
    int32_t minY = 0;
    int32_t maxX = 0;
    int32_t maxY = 0;
};

struct PlatformMovementSettings
{
    uint32_t geometryVersion = 0;
    MovementMode movementMode = MovementMode::Free;
    uint32_t spawnFootholdId = 0;
    uint32_t halfWidth = 0;
    uint32_t halfHeight = 0;
    uint32_t horizontalSpeed = 0;
    uint32_t jumpSpeed = 0;
    uint32_t gravity = 0;
    uint32_t maxFallSpeed = 0;
};

struct MapGeometry
{
    MapDefinition map;
    PlatformMovementSettings movement;
    std::vector<FootholdDefinition> footholds;
    std::vector<MapCollisionDefinition> colliders;

    const FootholdDefinition* FindFoothold(uint32_t footholdId) const
    {
        for (const FootholdDefinition& foothold : footholds)
        {
            if (foothold.footholdId == footholdId)
                return &foothold;
        }

        return nullptr;
    }
};
