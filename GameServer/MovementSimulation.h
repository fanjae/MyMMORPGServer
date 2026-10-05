#pragma once

#include "MapGeometry.h"

struct PlatformMovementInput
{
    int8_t horizontal = 0;
    bool jumpHeld = false;
};

struct PlatformMovementState
{
    double x = 0.0;
    double y = 0.0;
    double velocityX = 0.0;
    double velocityY = 0.0;
    uint32_t footholdId = 0;
    bool grounded = false;
    bool jumpHeld = false;
};

enum class SimulationResult : uint8_t
{
    Success = 0,
    Respawned = 1,
    InvalidInput = 2,
    InvalidState = 3,
    NotPlatformer = 4
};

class MovementSimulation
{
public:
    static constexpr double FixedDeltaSeconds = 0.02;
    static constexpr double Epsilon = 0.000001;

    explicit MovementSimulation(const MapGeometry& geometry);
    MovementSimulation(MapGeometry&&) = delete;

    bool Reset(PlatformMovementState& state) const;
    SimulationResult Step(PlatformMovementState& state, const PlatformMovementInput& input) const;

private:
    struct Collision
    {
        double time = 1.0;
        bool hit = false;
        bool blockX = false;
        bool blockY = false;
        bool respawn = false;
        uint32_t footholdId = 0;
    };

    bool IsValidState(const PlatformMovementState& state) const;
    const FootholdDefinition* FindSupport(double x, uint32_t footholdId) const;
    Collision FindCollision(double x, double y, double dx, double dy) const;
    double MoveGround(PlatformMovementState& state, double dx) const;
    SimulationResult MoveAir(PlatformMovementState& state, double seconds) const;

    const MapGeometry& _geometry;
};
