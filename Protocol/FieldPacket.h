#pragma once
#include <cstdint>

// 장비·소비·설치·기타·캐시 분류마다 24개 슬롯을 같은 순서로 전송한다.
constexpr uint8_t INVENTORY_CATEGORIES = 5;
constexpr uint8_t INVENTORY_SLOTS = 24;
constexpr uint16_t MAX_FIELD_MONSTERS = 128; // 전체 목록이 한 패킷에 들어가는 범위로 제한한다.
enum class InventoryOperation : uint8_t { Refresh, Move, Sort, Use };
enum class InventoryResult : uint8_t { Success, InvalidSlot, StaleRevision, CannotUse, StorageError, RateLimited };
#pragma pack(push, 1)
struct InventoryEntry { uint32_t itemId = 0; uint16_t quantity = 0; };
struct InventoryRequestPacket
{
    // 소유 캐릭터는 인증된 세션에서 결정하고 클라이언트는 조작과 변경 번호만 보낸다.
    InventoryOperation operation = InventoryOperation::Refresh;
    uint8_t category = 0, source = 0, target = 0;
    uint64_t revision = 0;
};
struct InventorySnapshotPacket
{
    // 요청 결과와 전체 상태를 함께 보내 중복 요청 거절 시에도 최신 상태로 복구한다.
    uint64_t revision = 1, mesos = 1250;
    uint16_t hp = 50, maxHp = 100, mp = 25, maxMp = 60;
    InventoryResult result = InventoryResult::Success;
    uint8_t slotsPerCategory = INVENTORY_SLOTS;
    InventoryEntry entries[INVENTORY_CATEGORIES * INVENTORY_SLOTS]{};
};
// 헤더 뒤에 count개 레코드가 이어지며 입장 번호와 tick으로 이전 상태를 구분한다.
struct FieldMonstersHeader { uint32_t mapId = 0; uint64_t generation = 0, tick = 0; uint16_t count = 0; };
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
static_assert(sizeof(InventoryRequestPacket) == 12);
static_assert(sizeof(InventorySnapshotPacket) == 746);
static_assert(sizeof(FieldMonstersHeader) == 22);
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
