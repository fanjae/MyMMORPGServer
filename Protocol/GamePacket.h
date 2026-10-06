#pragma once

#include <cstdint>
#include "CharacterName.h"

constexpr uint32_t MAX_PLAYER_NAME_LENGTH = MAX_CHARACTER_NAME_LENGTH;
constexpr uint32_t MAX_CHAT_MESSAGE_LENGTH = 128;
constexpr uint32_t GAME_PROTOCOL_VERSION = 4;

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
    MapInfo = 13,
    MovementInput = 14,
    MovementState = 15,
    MapGeometry = 16,
    Foothold = 17,
    Collider = 18,
    GeometryEnd = 19,
    WhisperRequest = 20,
    ChatResponse = 21,
    WhisperMessage = 22
};

enum class EnterGameResult : uint8_t
{
    Success = 0,
    InvalidAuthKey = 1,
    AlreadyAuthenticated = 2,
    CharacterLoadFailed = 3,
    AlreadyInGame = 4,
    MapEnterFailed = 5,
    ProtocolMismatch = 6,
    AuthenticationPending = 7
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
    InvalidSequence = 4,
    WrongMovementMode = 5
};

enum class MovementStateReason : uint8_t { Normal = 0, Respawned = 1, InputRejected = 2, InputExpired = 3 };
enum class ChatOperation : uint8_t { Map = 0, Whisper = 1 };
enum class ChatResult : uint8_t { InvalidMessage = 0, RateLimited = 1, TargetNotFound = 2, DeliveryFailed = 3, InvalidTarget = 4 };

#pragma pack(push, 1)

struct EnterGameRequest
{
    uint64_t authKey = 0;
    uint32_t protocolVersion = GAME_PROTOCOL_VERSION;
};

struct MovementInputPacket
{
    uint32_t mapId = 0;
    uint64_t generation = 0;
    uint64_t sequence = 0;
    int8_t horizontal = 0;
    uint8_t jumpHeld = 0;
};

struct MovementStatePacket
{
    uint32_t mapId = 0;
    uint64_t generation = 0;
    uint32_t characterId = 0;
    uint64_t serverTick = 0;
    uint64_t sequence = 0;
    int64_t x = 0;
    int64_t y = 0;
    int64_t velocityX = 0;
    int64_t velocityY = 0;
    uint32_t footholdId = 0;
    uint8_t grounded = 0;
    MovementStateReason reason = MovementStateReason::Normal;
};

struct MapGeometryPacket
{
    uint32_t mapId = 0;
    uint64_t generation = 0;
    uint32_t version = 0;
    uint8_t mode = 0;
    uint32_t halfWidth = 0;
    uint32_t halfHeight = 0;
    uint32_t horizontalSpeed = 0;
    uint32_t jumpSpeed = 0;
    uint32_t gravity = 0;
    uint32_t maxFallSpeed = 0;
    int32_t spawnX = 0;
    int32_t spawnY = 0;
    uint32_t spawnFootholdId = 0;
    uint16_t footholdCount = 0;
    uint16_t colliderCount = 0;
};

struct FootholdPacket
{
    uint32_t mapId = 0;
    uint64_t generation = 0;
    uint32_t id = 0;
    int32_t x1 = 0;
    int32_t y1 = 0;
    int32_t x2 = 0;
    int32_t y2 = 0;
    uint32_t prevId = 0;
    uint32_t nextId = 0;
};

struct ColliderPacket
{
    uint32_t mapId = 0;
    uint64_t generation = 0;
    uint32_t id = 0;
    int32_t minX = 0;
    int32_t minY = 0;
    int32_t maxX = 0;
    int32_t maxY = 0;
};

struct GeometryEndPacket
{
    uint32_t mapId = 0;
    uint64_t generation = 0;
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

struct WhisperRequest
{
    uint32_t targetCharacterId = 0;
    char message[MAX_CHAT_MESSAGE_LENGTH] = {};
};

struct ChatResponse
{
    ChatOperation operation = ChatOperation::Map;
    ChatResult result = ChatResult::InvalidMessage;
    uint32_t targetCharacterId = 0;
    uint32_t retryAfterMs = 0;
};

struct WhisperMessage
{
    uint32_t senderCharacterId = 0;
    uint32_t targetCharacterId = 0;
    char senderName[MAX_PLAYER_NAME_LENGTH] = {};
    char targetName[MAX_PLAYER_NAME_LENGTH] = {};
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
static_assert(sizeof(MovementInputPacket) == 22);
static_assert(sizeof(MovementStatePacket) == 70);
static_assert(sizeof(MapGeometryPacket) == 57);
static_assert(sizeof(WhisperRequest) == 132);
static_assert(sizeof(ChatResponse) == 10);
static_assert(sizeof(WhisperMessage) == 266);
