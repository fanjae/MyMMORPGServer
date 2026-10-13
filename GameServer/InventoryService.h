#pragma once
#include "../Protocol/FieldPacket.h"

// DB 접근 없이 인벤토리 상태를 검증하고 이동·정렬·사용 규칙을 적용한다.
class InventoryService
{
public:
    static InventorySnapshotPacket Apply(const InventorySnapshotPacket& state, const InventoryRequestPacket& request);
    static bool Validate(const InventorySnapshotPacket& state);
    static InventorySnapshotPacket Starter();
    static uint16_t StackLimit(uint32_t itemId, uint8_t category);
    static int EquipmentSlot(uint32_t itemId) { return itemId == 1 ? 0 : itemId == 2 ? 1 : -1; }
    static InventorySnapshotPacket Mutate(const InventorySnapshotPacket& state, const InventoryMutation& mutation);
};
