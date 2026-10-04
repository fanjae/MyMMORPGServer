#pragma once

#include <algorithm>
#include <chrono>

class MovementValidator
{
public:
    using Clock = std::chrono::steady_clock;

    void Reset(double speed, double burst, Clock::time_point now = Clock::now())
    {
        _speed = speed;
        _burst = burst;
        _availableDistance = burst;
        _updatedAt = now;
    }

    bool TryConsume(double distance, Clock::time_point now = Clock::now())
    {
        // 정지 시간이나 요청 횟수로 이동 허용량이 무한히 증가하지 않도록 상한을 유지한다.
        double elapsed = std::chrono::duration<double>(now - _updatedAt).count();
        _availableDistance = (std::min)(_burst, _availableDistance + (std::max)(0.0, elapsed) * _speed);
        _updatedAt = now;

        if (distance < 0.0 || distance > _availableDistance)
            return false;

        _availableDistance -= distance;
        return true;
    }

private:
    double _speed = 0.0;
    double _burst = 0.0;
    double _availableDistance = 0.0;
    Clock::time_point _updatedAt;
};
