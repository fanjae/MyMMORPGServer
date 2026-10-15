#pragma once
#include "../Protocol/TradePacket.h"
#include "InventoryService.h"
#include <algorithm>
#include <limits>
#include <unordered_set>

// 양쪽 제안은 제거를 모두 계산한 뒤 지급한다. 빈 칸이 없는 장비 맞교환도 허용한다.
inline TradeResult ApplyTrade(const TradeCommitRequest& request, InventorySnapshotPacket (&states)[2])
{
    InventorySnapshotPacket next[2]{states[0],states[1]};
    if (!request.tradeId || !request.characters[0] || request.characters[0] == request.characters[1]) return TradeResult::Invalid;
    for (int side = 0; side < 2; ++side)
    {
        const auto& offer = request.offers[side]; auto& state = next[side];
        if (!InventoryService::Validate(state) || !state.hp || offer.count > MAX_TRADE_ITEMS) return TradeResult::Invalid;
        if (offer.inventoryRevision != state.revision) return TradeResult::Stale;
        if (offer.mesos > state.mesos) return TradeResult::Invalid;
        std::unordered_set<unsigned> slots;
        for (uint8_t i = 0; i < offer.count; ++i)
        {
            const auto& item = offer.items[i]; const auto& source = item.source;
            if (source.category >= 5 || source.slot >= 24 || !source.quantity || !slots.insert(source.category*24+source.slot).second) return TradeResult::Invalid;
            auto& entry = state.entries[source.category*24+source.slot];
            if (!item.itemId || entry.itemId != item.itemId || entry.instanceId != item.instanceId || source.quantity > entry.quantity) return TradeResult::Stale;
            entry.quantity -= source.quantity; if (!entry.quantity) entry = {};
        }
        state.mesos -= offer.mesos;
    }
    for (int side = 0; side < 2; ++side)
    {
        auto& state = next[side]; const auto& incoming = request.offers[1-side];
        if (state.mesos > (std::numeric_limits<uint64_t>::max)() - incoming.mesos) return TradeResult::Invalid;
        state.mesos += incoming.mesos;
        for (uint8_t i = 0; i < incoming.count; ++i)
        {
            auto item = incoming.items[i]; uint16_t remaining = item.source.quantity;
            uint8_t category = item.source.category; auto limit = InventoryService::StackLimit(item.itemId,category);
            if (!limit) return TradeResult::Invalid;
            for (int pass = 0; pass < 2 && remaining; ++pass) for (int slot = category*24; slot < (category+1)*24 && remaining; ++slot)
            {
                auto& entry = state.entries[slot];
                if (pass == 0 ? entry.itemId != item.itemId || entry.instanceId != 0 || item.instanceId != 0 : entry.itemId != 0) continue;
                auto quantity = (std::min)(remaining, uint16_t(limit-entry.quantity));
                if (quantity) { entry.itemId=item.itemId; entry.instanceId=item.instanceId; entry.quantity+=quantity; remaining-=quantity; }
            }
            if (remaining) return TradeResult::Full;
        }
        ++state.revision;
        if (!InventoryService::Validate(state)) return TradeResult::Invalid;
    }
    states[0]=next[0]; states[1]=next[1]; return TradeResult::Success;
}
