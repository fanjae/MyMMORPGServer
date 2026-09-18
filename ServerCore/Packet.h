#pragma once

#include <cstdint>

#pragma pack(push, 1)

struct PacketHeader
{
    uint16_t size = 0;
    uint16_t opcode = 0;
};

#pragma pack(pop)