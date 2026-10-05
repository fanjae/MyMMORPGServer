#include "MovementSimulation.h"
#include "MapGeometryLoader.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
    bool SweepAxis(double position, double delta, double minimum, double maximum, double& entry, double& exit)
    {
        if (std::abs(delta) <= MovementSimulation::Epsilon)
            return position > minimum + MovementSimulation::Epsilon && position < maximum - MovementSimulation::Epsilon;

        entry = (minimum - position) / delta;
        exit = (maximum - position) / delta;
        if (entry > exit)
            std::swap(entry, exit);

        return true;
    }
}

MovementSimulation::MovementSimulation(const MapGeometry& geometry) : _geometry(geometry)
{
    std::string error;
    if (!MapGeometryLoader::Validate(geometry, error))
        throw std::invalid_argument(error);
}

bool MovementSimulation::Reset(PlatformMovementState& state) const
{
    if (_geometry.movement.movementMode != MovementMode::Platformer)
        return false;

    state = {};
    state.x = _geometry.map.spawnX;
    state.y = _geometry.map.spawnY;
    state.footholdId = _geometry.movement.spawnFootholdId;
    state.grounded = true;
    return true;
}

bool MovementSimulation::IsValidState(const PlatformMovementState& state) const
{
    if (!std::isfinite(state.x) || !std::isfinite(state.y) || !std::isfinite(state.velocityX) || !std::isfinite(state.velocityY))
        return false;

    const auto& map = _geometry.map;
    const auto& movement = _geometry.movement;
    if (state.x < map.minX + static_cast<double>(movement.halfWidth) - Epsilon || state.x > map.maxX - static_cast<double>(movement.halfWidth) + Epsilon || state.y < map.minY + static_cast<double>(movement.halfHeight) - Epsilon || state.y > map.maxY - static_cast<double>(movement.halfHeight) + Epsilon)
        return false;

    if (std::abs(state.velocityX) > movement.horizontalSpeed + Epsilon || state.velocityY > movement.jumpSpeed + Epsilon || state.velocityY < -static_cast<double>(movement.maxFallSpeed) - Epsilon)
        return false;

    for (const MapCollisionDefinition& collider : _geometry.colliders)
    {
        if (state.x > collider.minX - static_cast<double>(movement.halfWidth) + Epsilon && state.x < collider.maxX + static_cast<double>(movement.halfWidth) - Epsilon && state.y > collider.minY - static_cast<double>(movement.halfHeight) + Epsilon && state.y < collider.maxY + static_cast<double>(movement.halfHeight) - Epsilon)
            return false;
    }

    const FootholdDefinition* foothold = _geometry.FindFoothold(state.footholdId);
    if (state.grounded)
        return foothold != nullptr && state.x >= foothold->x1 - Epsilon && state.x <= foothold->x2 + Epsilon && std::abs(state.y - foothold->y1 - movement.halfHeight) <= Epsilon && std::abs(state.velocityY) <= Epsilon;

    return state.footholdId == 0;
}

const FootholdDefinition* MovementSimulation::FindSupport(double x, uint32_t footholdId) const
{
    const FootholdDefinition* foothold = _geometry.FindFoothold(footholdId);
    for (size_t i = 0; foothold != nullptr && i < _geometry.footholds.size(); ++i)
    {
        if (x < foothold->x1 - Epsilon)
            foothold = _geometry.FindFoothold(foothold->prevId);
        else if (x > foothold->x2 + Epsilon)
            foothold = _geometry.FindFoothold(foothold->nextId);
        else
            return foothold;
    }

    return nullptr;
}

MovementSimulation::Collision MovementSimulation::FindCollision(double x, double y, double dx, double dy) const
{
    Collision nearest;
    auto consider = [&](double time, bool blockX, bool blockY, uint32_t footholdId, bool respawn)
    {
        if (time < -Epsilon || time > 1.0 + Epsilon)
            return;

        time = std::clamp(time, 0.0, 1.0);
        if (!nearest.hit || time < nearest.time - Epsilon)
            nearest = { time, true, blockX, blockY, respawn, footholdId };
        else if (std::abs(time - nearest.time) <= Epsilon)
        {
            nearest.blockX = nearest.blockX || blockX;
            nearest.blockY = nearest.blockY || blockY;
            nearest.respawn = nearest.respawn || respawn;
            if (footholdId != 0 && (nearest.footholdId == 0 || footholdId < nearest.footholdId))
                nearest.footholdId = footholdId;
        }

        if (nearest.footholdId != 0)
            nearest.respawn = false;
    };

    const auto& movement = _geometry.movement;
    for (const MapCollisionDefinition& collider : _geometry.colliders)
    {
        double entryX = -std::numeric_limits<double>::infinity();
        double entryY = -std::numeric_limits<double>::infinity();
        double exitX = std::numeric_limits<double>::infinity();
        double exitY = std::numeric_limits<double>::infinity();
        if (!SweepAxis(x, dx, collider.minX - static_cast<double>(movement.halfWidth), collider.maxX + static_cast<double>(movement.halfWidth), entryX, exitX) || !SweepAxis(y, dy, collider.minY - static_cast<double>(movement.halfHeight), collider.maxY + static_cast<double>(movement.halfHeight), entryY, exitY))
            continue;

        double entry = (std::max)(entryX, entryY);
        double exit = (std::min)(exitX, exitY);
        if (entry >= -Epsilon && exit > (std::max)(0.0, entry) + Epsilon)
            consider(entry, entryX >= entryY - Epsilon, entryY >= entryX - Epsilon, 0, false);
    }

    // 목적지가 아닌 교차 시점의 X를 검사해 빠른 낙하에서도 첫 발판에 착지한다.
    if (dy < -Epsilon)
    {
        for (const FootholdDefinition& foothold : _geometry.footholds)
        {
            double time = (foothold.y1 + static_cast<double>(movement.halfHeight) - y) / dy;
            double crossingX = x + dx * time;
            if (crossingX < foothold.x1 - Epsilon || crossingX > foothold.x2 + Epsilon)
                continue;

            if (time <= Epsilon && ((crossingX <= foothold.x1 + Epsilon && dx < 0.0) || (crossingX >= foothold.x2 - Epsilon && dx > 0.0)))
                continue;

            consider(time, false, true, foothold.footholdId, false);
        }
    }

    const auto& map = _geometry.map;
    if (dx < -Epsilon)
        consider((map.minX + static_cast<double>(movement.halfWidth) - x) / dx, true, false, 0, false);
    else if (dx > Epsilon)
        consider((map.maxX - static_cast<double>(movement.halfWidth) - x) / dx, true, false, 0, false);

    if (dy > Epsilon)
        consider((map.maxY - static_cast<double>(movement.halfHeight) - y) / dy, false, true, 0, false);
    else if (dy < -Epsilon)
        consider((map.minY + static_cast<double>(movement.halfHeight) - y) / dy, false, true, 0, true);

    return nearest;
}

double MovementSimulation::MoveGround(PlatformMovementState& state, double dx) const
{
    const FootholdDefinition* current = _geometry.FindFoothold(state.footholdId);
    const FootholdDefinition* last = current;
    double target = state.x + dx;
    for (size_t i = 0; i < _geometry.footholds.size(); ++i)
    {
        uint32_t neighborId = target > last->x2 + Epsilon ? last->nextId : target < last->x1 - Epsilon ? last->prevId : 0;
        const FootholdDefinition* neighbor = _geometry.FindFoothold(neighborId);
        if (neighbor == nullptr)
            break;

        last = neighbor;
    }

    double groundDx = std::clamp(target, static_cast<double>(last->x1), static_cast<double>(last->x2)) - state.x;
    Collision collision = FindCollision(state.x, state.y, groundDx, 0.0);
    if (collision.hit)
    {
        state.x += groundDx * collision.time;
        state.velocityX = 0.0;
        state.footholdId = FindSupport(state.x, current->footholdId)->footholdId;
        return 0.0;
    }

    state.x += groundDx;
    state.footholdId = FindSupport(state.x, current->footholdId)->footholdId;
    double remaining = dx - groundDx;
    if (std::abs(remaining) > Epsilon)
    {
        state.grounded = false;
        state.footholdId = 0;
    }

    return remaining;
}

SimulationResult MovementSimulation::MoveAir(PlatformMovementState& state, double seconds) const
{
    state.velocityY = (std::max)(-static_cast<double>(_geometry.movement.maxFallSpeed), state.velocityY - _geometry.movement.gravity * seconds);
    double dx = state.velocityX * seconds;
    double dy = state.velocityY * seconds;

    for (size_t i = 0; i < 8 && (std::abs(dx) > Epsilon || std::abs(dy) > Epsilon); ++i)
    {
        Collision collision = FindCollision(state.x, state.y, dx, dy);
        state.x += dx * collision.time;
        state.y += dy * collision.time;
        if (!collision.hit)
            break;

        if (collision.respawn)
        {
            bool jumpHeld = state.jumpHeld;
            Reset(state);
            state.jumpHeld = jumpHeld;
            return SimulationResult::Respawned;
        }

        dx *= 1.0 - collision.time;
        dy *= 1.0 - collision.time;
        if (collision.blockX)
        {
            state.velocityX = 0.0;
            dx = 0.0;
        }

        if (collision.blockY)
        {
            state.velocityY = 0.0;
            dy = 0.0;
        }

        if (collision.footholdId != 0)
        {
            state.grounded = true;
            state.footholdId = collision.footholdId;
            state.y = _geometry.FindFoothold(collision.footholdId)->y1 + static_cast<double>(_geometry.movement.halfHeight);
            dx = MoveGround(state, dx);
        }
    }

    return SimulationResult::Success;
}

SimulationResult MovementSimulation::Step(PlatformMovementState& state, const PlatformMovementInput& input) const
{
    if (_geometry.movement.movementMode != MovementMode::Platformer)
        return SimulationResult::NotPlatformer;

    if (input.horizontal < -1 || input.horizontal > 1)
        return SimulationResult::InvalidInput;

    if (!IsValidState(state))
        return SimulationResult::InvalidState;

    PlatformMovementState next = state;
    bool jumpPressed = input.jumpHeld && !next.jumpHeld;
    next.jumpHeld = input.jumpHeld;
    next.velocityX = input.horizontal * static_cast<double>(_geometry.movement.horizontalSpeed);
    if (next.grounded && jumpPressed)
    {
        next.grounded = false;
        next.footholdId = 0;
        next.velocityY = _geometry.movement.jumpSpeed;
    }

    double seconds = FixedDeltaSeconds;
    if (next.grounded)
    {
        double dx = next.velocityX * seconds;
        double remaining = MoveGround(next, dx);
        if (next.grounded)
        {
            state = next;
            return SimulationResult::Success;
        }

        seconds *= std::abs(remaining / dx);
    }

    SimulationResult result = MoveAir(next, seconds);
    state = next;
    return result;
}
