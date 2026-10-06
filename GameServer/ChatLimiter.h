#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <unordered_map>

class ChatLimiter
{
public:
    using Clock = std::chrono::steady_clock;
    static constexpr double Capacity = 5.0;

    bool TryConsume(uint32_t accountId, uint32_t& retryAfterMs, Clock::time_point now = Clock::now())
    {
        auto entry = _buckets.try_emplace(accountId, Bucket{Capacity, now}).first;
        Refill(entry->second, now);
        retryAfterMs = 0;
        if (entry->second.tokens < 1.0)
        {
            retryAfterMs = static_cast<uint32_t>(std::ceil((1.0 - entry->second.tokens) * 1000.0));
            return false;
        }
        entry->second.tokens -= 1.0;
        return true;
    }

    void Cleanup(Clock::time_point now = Clock::now())
    {
        for (auto entry = _buckets.begin(); entry != _buckets.end();)
        {
            Refill(entry->second, now);
            if (entry->second.tokens >= Capacity)
                entry = _buckets.erase(entry);
            else
                ++entry;
        }
    }

private:
    struct Bucket { double tokens; Clock::time_point updated; };
    static void Refill(Bucket& bucket, Clock::time_point now)
    {
        if (now > bucket.updated)
        {
            // 초당 한 개를 회복하고 거절 요청으로 회복 시간을 초기화하지 않는다.
            bucket.tokens = (std::min)(Capacity, bucket.tokens + std::chrono::duration<double>(now - bucket.updated).count());
            bucket.updated = now;
        }
    }
    std::unordered_map<uint32_t, Bucket> _buckets;
};
