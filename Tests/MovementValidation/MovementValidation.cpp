#include "../../GameServer/MapDefinition.h"
#include "../../GameServer/Player.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
}

int main()
{
    try
    {
        MovementValidator movement;
        MovementValidator::Clock::time_point start{};
        movement.Reset(80.0, 12.0, start);
        Check(movement.TryConsume(12.0, start), "Initial burst");
        Check(!movement.TryConsume(1.0, start), "Repeated requests created extra allowance");
        Check(movement.TryConsume(4.0, start + std::chrono::milliseconds(50)), "50ms normal movement");
        Check(!movement.TryConsume(1.0, start + std::chrono::milliseconds(50)), "Allowance was not consumed");
        std::cout << "[PASS] Speed budget and repeated requests\n";

        Check(!movement.TryConsume(13.0, start + std::chrono::hours(1)), "Idle created unlimited allowance");
        Check(movement.TryConsume(12.0, start + std::chrono::hours(1)), "Rejected movement consumed allowance");
        std::cout << "[PASS] Idle cap and rejection recovery\n";

        movement.Reset(80.0, 12.0, start);
        Check(!movement.TryConsume(std::hypot(12.0, 12.0), start), "Diagonal bypassed distance limit");
        double extremeDistance = static_cast<double>((std::numeric_limits<int32_t>::max)()) - (std::numeric_limits<int32_t>::min)();
        Check(!movement.TryConsume(extremeDistance, start), "Extreme distance accepted");
        Check(movement.TryConsume(12.0, start), "Reset movement budget");
        std::cout << "[PASS] Diagonal distance, extreme coordinates and reset\n";

        MapDefinition map{ 100000000, 0, 0, -400, 400, -200, 200, 80, 12 };
        Check(map.IsValid() && map.Contains(400, 200) && !map.Contains(401, 200), "Inclusive boundary");
        map.spawnX = 401;
        Check(!map.IsValid(), "Invalid spawn accepted");
        map.spawnX = 0;
        map.moveSpeed = 0;
        Check(!map.IsValid(), "Invalid speed accepted");
        std::cout << "[PASS] Map bounds and configuration\n";

        Player player(1001, 1, "Warrior", 10);
        Check(!player.AcceptMoveSequence(0) && player.AcceptMoveSequence(1), "Initial sequence");
        Check(!player.AcceptMoveSequence(1) && !player.AcceptMoveSequence(0) && player.AcceptMoveSequence(2), "Duplicate sequence accepted");
        player.SetMap(nullptr);
        Check(!player.AcceptMoveSequence(1), "Map switch reset sequence");
        std::cout << "[PASS] Sequence replay rejection\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[FAIL] " << exception.what() << '\n';
        return 1;
    }
}
