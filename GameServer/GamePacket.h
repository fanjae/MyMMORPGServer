#pragma once

#include <cstdint>

enum class GamePacketOpcode : uint16_t
{
    EnterGameRequest = 1,
    EnterGameResponse = 2
};

enum class EnterGameResult : uint8_t
{
    Success = 0,
    InvalidAuthKey = 1,
    AlreadyAuthenticated = 2
};

#pragma pack(push, 1)

struct EnterGameRequest
{
    uint64_t authKey = 0;
};

struct EnterGameResponse
{
    EnterGameResult result = EnterGameResult::InvalidAuthKey;
};

#pragma pack(pop)