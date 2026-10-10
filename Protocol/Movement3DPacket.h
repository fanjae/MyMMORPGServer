#pragma once
#include "GamePacket.h"

// 2D wire format은 유지하고 3D 연결은 별도 opcode와 version 7을 사용한다.
#pragma pack(push, 1)
struct MovementAction3D
{
    uint64_t sequence = 0, clientTick = 0, jumpId = 0;
    int64_t x = 0, y = 0, z = 0;
    int64_t velocityX = 0, velocityY = 0, velocityZ = 0;
    uint32_t supportId = 0;
    int16_t inputX = 0, inputZ = 0;
    uint16_t yaw = 0; // 0..35999, 1/100도 단위.
    uint8_t grounded = 1;
    MovementActionKind kind = MovementActionKind::Checkpoint;
};
struct RelayedMovementAction3D
{
    uint32_t characterId = 0;
    uint64_t latestClientTick = 0;
    MovementAction3D action;
};
struct MovementState3DPacket
{
    uint32_t mapId = 0;
    uint64_t generation = 0;
    RelayedMovementAction3D state;
};
struct World3DPacket
{
    uint32_t mapId = 0;
    uint64_t generation = 0;
    uint32_t version = 0;
    int32_t minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;
    uint32_t radius = 0, height = 0, speed = 0, jumpSpeed = 0, gravity = 0, maxFallSpeed = 0;
    int32_t spawnX = 0, spawnY = 0, spawnZ = 0;
    uint16_t boxCount = 0;
};
struct WorldBox3DPacket
{
    uint32_t mapId = 0;
    uint64_t generation = 0;
    uint32_t id = 0;
    int32_t minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;
};
#pragma pack(pop)
static_assert(sizeof(MovementAction3D) == 84);
static_assert(sizeof(RelayedMovementAction3D) == 96);
static_assert(sizeof(MovementState3DPacket) == 108);
static_assert(sizeof(World3DPacket) == 78);
static_assert(sizeof(WorldBox3DPacket) == 40);
