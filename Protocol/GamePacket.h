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
    PlayerChat = 10,
    MonsterEnterMap = 11,
    MoveResponse = 12,
    MapInfo = 13
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

enum class MoveResult : uint8_t
{
    Success = 0,
    OutOfBounds = 1,
    SpeedExceeded = 2,
    MapMismatch = 3,
    InvalidSequence = 4
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
    uint32_t mapId = 0;
    uint64_t sequence = 0;
    int32_t x = 0;
    int32_t y = 0;
};

struct MoveResponse
{
    uint64_t sequence = 0;
    uint32_t mapId = 0;
    MoveResult result = MoveResult::Success;
    int32_t x = 0;
    int32_t y = 0;
};

struct MapInfo
{
    uint32_t mapId = 0;
    int32_t minX = 0;
    int32_t maxX = 0;
    int32_t minY = 0;
    int32_t maxY = 0;
    uint32_t moveSpeed = 0;
    uint32_t moveBurst = 0;
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
struct MonsterEnterMap
{
    uint32_t monsterId = 0;
    int32_t x = 0;
    int32_t y = 0;
};

#pragma pack(pop)

static_assert(sizeof(MoveRequest) == 20);
static_assert(sizeof(MoveResponse) == 21);
static_assert(sizeof(MapInfo) == 28);
