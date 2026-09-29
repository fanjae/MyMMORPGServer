#pragma once

#include <cstdint>

constexpr uint32_t MAX_PLAYER_NAME_LENGTH = 16;
constexpr uint32_t MAX_CHAT_MESSAGE_LENGTH = 128;

enum class GamePacketOpcode : uint16_t
{
    EnterGameRequest = 1,
    EnterGameResponse = 2,
    PlayerEnterMap = 3,
    PlayerLeaveMap = 4,
    MoveRequest = 5,
    PlayerMove = 6,
    ChangeMapRequest = 7,
    ChangeMapResponse = 8,
    ChatRequest = 9,
    PlayerChat = 10
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

enum class ChangeMapResult : uint8_t
{
    Success = 0,
    MapNotFound = 1,
    AlreadyInMap = 2,
    MapEnterFailed = 3
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
    int32_t x = 0;
    int32_t y = 0;
};

struct PlayerEnterMap
{
    uint32_t characterId = 0;
    char name[MAX_PLAYER_NAME_LENGTH] = {};
    uint16_t level = 0;
    int32_t x = 0;
    int32_t y = 0;
};

struct PlayerLeaveMap
{
    uint32_t characterId = 0;
};

struct MoveRequest
{
    int32_t x = 0;
    int32_t y = 0;
};

struct PlayerMove
{
    uint32_t characterId = 0;
    int32_t x = 0;
    int32_t y = 0;
};

struct ChangeMapRequest
{
    uint32_t mapId = 0;
};

struct ChangeMapResponse
{
    ChangeMapResult result = ChangeMapResult::MapNotFound;
    uint32_t mapId = 0;
    int32_t x = 0;
    int32_t y = 0;
};

struct ChatRequest
{
    char message[MAX_CHAT_MESSAGE_LENGTH] = {};
};

struct PlayerChat
{
    uint32_t characterId = 0;
    char message[MAX_CHAT_MESSAGE_LENGTH] = {};
};

#pragma pack(pop)