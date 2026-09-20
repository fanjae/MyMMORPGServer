#pragma once

#include <cstdint>

enum class ServerPacketOpcode : uint16_t
{
    RegisterAuthTicketRequest = 1,
    RegisterAuthTicketResponse = 2
};

enum class RegisterAuthTicketResult : uint8_t
{
    Success = 0,
    Failed = 1
};

#pragma pack(push, 1)

struct RegisterAuthTicketRequest
{
    uint32_t accountId = 0;
    uint32_t characterId = 0;
    uint64_t authKey = 0;
};

struct RegisterAuthTicketResponse
{
    RegisterAuthTicketResult result = RegisterAuthTicketResult::Failed;
    uint64_t authKey = 0;
};

#pragma pack(pop)