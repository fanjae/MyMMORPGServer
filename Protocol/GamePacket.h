#pragma once

#include <cstdint>

constexpr uint32_t MAX_PLAYER_NAME_LENGTH = 16;

enum class GamePacketOpcode : uint16_t
{
    EnterGameRequest = 1,
    EnterGameResponse = 2
};

enum class EnterGameResult : uint8_t
{
    Success = 0,
    InvalidAuthKey = 1,
    AlreadyAuthenticated = 2,
    CharacterLoadFailed = 3,
    AlreadyInGame = 4,
    MapEnterFailed = 5
};

#pragma pack(push, 1)

struct EnterGameRequest
{
    uint64_t authKey = 0;
};

struct EnterGameResponse
{
    EnterGameResult result = EnterGameResult::InvalidAuthKey;
    uint32_t characterId = 0;
    char name[MAX_PLAYER_NAME_LENGTH] = {};
    uint16_t level = 0;
};

#pragma pack(pop)