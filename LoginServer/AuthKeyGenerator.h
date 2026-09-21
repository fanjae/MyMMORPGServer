#pragma once

#include <cstdint>

class AuthKeyGenerator
{
public:
    bool Generate(uint64_t& authKey);
};