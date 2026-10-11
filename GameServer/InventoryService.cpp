#include "InventoryService.h"
#include <algorithm>
#include <vector>

uint16_t InventoryService::StackLimit(uint32_t item, uint8_t category)
{
    // 아이템과 분류가 일치해야 하며 알 수 없는 아이템은 허용하지 않는다.
    switch (item)
    {
    case 1: case 2: return category == 0 ? 1 : 0;
    case 101: case 102: return category == 1 ? 100 : 0;
    case 201: return category == 2 ? 1 : 0;
    case 301: case 302: return category == 3 ? 200 : 0;
    case 401: return category == 4 ? 1 : 0;
    default: return 0;
    }
}
InventorySnapshotPacket InventoryService::Starter()
{
    // 저장된 인벤토리가 없는 캐릭터의 최초 생성에만 사용할 기본 상태.
    InventorySnapshotPacket state;
    state.entries[0] = {1, 1}; state.entries[1] = {2, 1};
    state.entries[24] = {101, 10}; state.entries[25] = {102, 8}; state.entries[28] = {101, 5};
    state.entries[48] = {201, 1}; state.entries[72] = {301, 25}; state.entries[73] = {302, 12};
    state.entries[96] = {401, 1}; return state;
}
bool InventoryService::Validate(const InventorySnapshotPacket& state)
{
    // 다음 변경 번호를 발급할 수 없는 상태와 HP·MP 범위 오류를 먼저 거절한다.
    if (!state.revision || state.revision == UINT64_MAX || state.slotsPerCategory != 24 ||
        !state.maxHp || !state.maxMp || state.hp > state.maxHp || state.mp > state.maxMp) return false;
    for (size_t i = 0; i < 120; ++i)
    {
        // 빈 슬롯은 아이템 번호와 수량이 모두 0이어야 한다.
        const auto& entry = state.entries[i];
        if (entry.quantity > StackLimit(entry.itemId, static_cast<uint8_t>(i / 24)) ||
            ((entry.itemId == 0) != (entry.quantity == 0))) return false;
    }
    return true;
}
InventorySnapshotPacket InventoryService::Apply(const InventorySnapshotPacket& state, const InventoryRequestPacket& request)
{
    if (!Validate(state)) { InventorySnapshotPacket error; error.result = InventoryResult::StorageError; return error; }
    auto reply = state; reply.result = InventoryResult::Success;
    if (request.operation == InventoryOperation::Refresh) return reply;
    if (request.operation > InventoryOperation::Use || request.category >= 5 || request.source >= 24 || request.target >= 24)
    { reply.result = InventoryResult::InvalidSlot; return reply; }
    if (request.revision != reply.revision) { reply.result = InventoryResult::StaleRevision; return reply; }
    // 클라이언트가 본 변경 번호가 현재 상태와 같을 때만 슬롯을 변경한다.
    auto& source = reply.entries[request.category * 24 + request.source];
    auto& target = reply.entries[request.category * 24 + request.target];
    if (request.operation == InventoryOperation::Move)
    {
        if (!source.itemId || request.source == request.target) { reply.result = InventoryResult::InvalidSlot; return reply; }
        if (source.itemId == target.itemId && StackLimit(source.itemId, request.category) > 1)
        {
            // 최대 중첩 수량까지만 옮기고 남은 수량은 원래 슬롯에 유지한다.
            uint16_t moved = (std::min)(source.quantity, static_cast<uint16_t>(StackLimit(source.itemId, request.category) - target.quantity));
            source.quantity -= moved; target.quantity += moved;
            if (!source.quantity) source = {};
        }
        else std::swap(source, target);
    }
    else if (request.operation == InventoryOperation::Sort)
    {
        // 현재 분류만 아이템 번호순으로 정렬하고 같은 아이템의 수량을 합친다.
        std::vector<InventoryEntry> items;
        auto begin = reply.entries + request.category * 24;
        for (size_t i = 0; i < 24; ++i) if (begin[i].itemId) items.push_back(begin[i]);
        std::sort(items.begin(), items.end(), [](auto a, auto b) { return a.itemId < b.itemId; });
        std::fill(begin, begin + 24, InventoryEntry{});
        size_t destination = 0;
        for (auto item : items)
        {
            if (destination && begin[destination - 1].itemId == item.itemId)
            {
                auto& previous = begin[destination - 1];
                auto moved = (std::min)(item.quantity, static_cast<uint16_t>(StackLimit(item.itemId, request.category) - previous.quantity));
                previous.quantity += moved; item.quantity -= moved;
            }
            if (item.quantity) begin[destination++] = item;
        }
    }
    else
    {
        // 회복할 HP·MP가 없으면 물약을 소비하지 않고 사용 실패를 반환한다.
        bool health = source.itemId == 101 && reply.hp < reply.maxHp;
        bool mana = source.itemId == 102 && reply.mp < reply.maxMp;
        if (request.category != 1 || !source.quantity || (!health && !mana)) { reply.result = InventoryResult::CannotUse; return reply; }
        if (health) reply.hp = static_cast<uint16_t>((std::min)(uint32_t{reply.maxHp}, uint32_t{reply.hp} + 30));
        else reply.mp = static_cast<uint16_t>((std::min)(uint32_t{reply.maxMp}, uint32_t{reply.mp} + 20));
        if (!--source.quantity) source = {};
    }
    // 성공한 변경의 새 번호를 반환해 같은 요청이 다시 적용되지 않도록 한다.
    ++reply.revision;
    return reply;
}
