#pragma once

#include <cstdint>

enum class LoginPacketOpcode : uint16_t
{
    LoginRequest = 1,
    LoginResponse = 2
};

enum class LoginResult : uint8_t
{
    Success = 0,
    InvalidAccount = 1
};

#pragma pack(push, 1)

struct LoginRequest
{
    uint32_t accountId = 0;
};

struct LoginResponse
{
    LoginResult result = LoginResult::InvalidAccount;
    uint64_t authKey = 0;
    uint16_t gameServerPort = 0;
};

#pragma pack(pop)