#pragma once
#include <atomic>
#include <cstdint>
#include <random>

// 실행 중 번호를 재사용하지 않고 서버 재시작 후에도 충돌 가능성을 낮춘다.
inline uint64_t NewFieldOperationId()
{
    static std::atomic<uint64_t> next{(uint64_t(std::random_device{}()) << 32) | std::random_device{}()};
    uint64_t id = ++next;
    return id ? id : ++next;
}
