#pragma once
#include <cstdint>

// 장비·소비·설치·기타·캐시 분류마다 24개 슬롯을 같은 순서로 전송한다.
constexpr uint8_t INVENTORY_CATEGORIES = 5;
constexpr uint8_t INVENTORY_SLOTS = 24;
constexpr uint16_t MAX_FIELD_MONSTERS = 128;
constexpr uint16_t FIELD_MONSTERS_PER_PART = 32; // 목록을 나눠 한 패킷의 크기와 수신 처리 단위를 제한한다.
enum class InventoryOperation : uint8_t { Refresh, Move, Sort, Use, Equip, Unequip };
enum class InventoryResult : uint8_t { Success, InvalidSlot, StaleRevision, CannotUse, StorageError, RateLimited, Full, AlreadyTaken };
#pragma pack(push, 1)
struct InventoryEntry { uint32_t itemId = 0; uint16_t quantity = 0; uint64_t instanceId = 0; };
struct InventoryRequestPacket
{
    // 소유 캐릭터는 인증된 세션에서 결정하고 클라이언트는 조작과 변경 번호만 보낸다.
    InventoryOperation operation = InventoryOperation::Refresh;
    uint8_t category = 0, source = 0, target = 0;
    uint64_t revision = 0;
    uint64_t requestId = 0; // 요청한 조작과 서버의 별도 상태 갱신 응답을 구분한다.
};
struct InventorySnapshotPacket
{
    // 요청 결과와 전체 상태를 함께 보내 중복 요청 거절 시에도 최신 상태로 복구한다.
    uint64_t revision = 1, mesos = 1250;
    uint16_t hp = 50, maxHp = 100, mp = 25, maxMp = 60;
    InventoryResult result = InventoryResult::Success;
    uint8_t slotsPerCategory = INVENTORY_SLOTS;
    InventoryEntry entries[INVENTORY_CATEGORIES * INVENTORY_SLOTS]{};
    InventoryEntry equipped[2]{}; // 무기·모자 순서. 인벤토리와 같은 고유 장비 인스턴스를 이동한다.
    uint64_t requestId = 0;
};
// 헤더 뒤에 count개 레코드가 이어지며 입장 번호와 tick으로 이전 상태를 구분한다.
struct FieldMonstersHeader { uint32_t mapId = 0; uint64_t generation = 0, tick = 0; uint16_t count = 0; };
// 몬스터 목록만 분할 정보를 추가하고 드롭의 기존 헤더는 유지한다.
struct FieldMonsterPartsHeader : FieldMonstersHeader { uint16_t part = 0, parts = 1, total = 0; };
struct FieldMonsterRecord
{
    uint32_t monsterId = 0;
    uint8_t kind = 0;
    int32_t x = 0, y = 0, z = 0; // 발 기준 좌표, 서버 단위의 1/1000 정밀도.
    uint16_t yaw = 0; // 0..35999, 1/100도 단위.
    uint16_t hp = 60, maxHp = 60;
    uint64_t revision = 1; // 피해와 재생성 순서를 비교해 늦은 상태가 HP를 되돌리지 않도록 한다.
};
#pragma pack(pop)
static_assert(sizeof(InventoryRequestPacket) == 20);
static_assert(sizeof(InventorySnapshotPacket) == 1742);
static_assert(sizeof(FieldMonstersHeader) == 22);
static_assert(sizeof(FieldMonsterPartsHeader) == 28);
static_assert(sizeof(FieldMonsterRecord) == 31);

enum class BasicAttackResult : uint8_t { Miss, Hit, Killed, Cooldown, InvalidState, Unavailable };
#pragma pack(push, 1)
// 공격 위치는 별도 클라이언트 좌표가 아닌 먼저 수신한 이동 상태를 사용한다.
struct BasicAttackRequest
{
    uint32_t mapId = 0; uint64_t generation = 0, sequence = 0, actionSequence = 0;
};
struct BasicAttackEvent
{
    uint32_t mapId = 0; uint64_t generation = 0, sequence = 0;
    uint32_t characterId = 0; BasicAttackResult result = BasicAttackResult::Miss;
    uint32_t monsterId = 0; uint16_t hp = 0, maxHp = 0; uint64_t revision = 0;
    uint16_t damage = 0, yaw = 0;
};
#pragma pack(pop)
static_assert(sizeof(BasicAttackRequest) == 28);
static_assert(sizeof(BasicAttackEvent) == 45);

// 서버 내부 작업 정보는 클라이언트의 인벤토리 요청과 분리한다.
struct InventoryMutation
{
    uint16_t damage = 0, quantity = 0; bool revive = false;
    uint64_t dropId = 0; uint32_t itemId = 0;
    uint64_t mutationId = 0; // 재시도할 피격은 같은 번호로 DB에서 한 번만 확정한다.
    bool queryDrop = false; // 획득 결과 조회는 지급 작업을 다시 실행하지 않는다.
};
enum class ChannelResult : uint8_t { Success, InvalidChannel, Busy, Cooldown, Unavailable };
enum class PickupResult : uint8_t { Success, NotFound, OutOfRange, Busy, Full, AlreadyTaken, StorageError, NoRecord };
#pragma pack(push, 1)
struct ChannelHeader { uint16_t current = 1, count = 3; ChannelResult result = ChannelResult::Success; };
struct ChannelRecord { uint16_t channel = 1; uint32_t players = 0; };
struct VitalsPacket
{
    uint32_t mapId = 0; uint64_t generation = 0; uint32_t characterId = 0;
    uint64_t revision = 0; uint16_t hp = 0, maxHp = 0, mp = 0, maxMp = 0, damage = 0;
    uint8_t paused = 0; // DB 결과가 불명확한 동안 이동·공격을 멈춘다.
};
struct DropRecord { uint64_t id = 0; uint32_t itemId = 0; uint16_t quantity = 0; int32_t x = 0, y = 0, z = 0; };
struct PickupRequest { uint32_t mapId = 0; uint64_t generation = 0, sequence = 0, dropId = 0; };
struct PickupResponse { uint64_t dropId = 0; PickupResult result = PickupResult::NotFound; };
#pragma pack(pop)
static_assert(sizeof(VitalsPacket) == 35);
static_assert(sizeof(DropRecord) == 26);
static_assert(sizeof(PickupRequest) == 28);
