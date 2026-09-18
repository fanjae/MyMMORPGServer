#pragma once

#include <cstdint>

enum class ServerPacketOpcode : uint16_t
{
    RegisterAuthTicket = 1
};

#pragma pack(push, 1)

struct RegisterAuthTicketRequest
{
    uint32_t accountId = 0;
    uint64_t authKey = 0;
};

#pragma pack(pop)