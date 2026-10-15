#pragma once
#include "FieldPacket.h"

constexpr uint8_t MAX_TRADE_ITEMS = 6;
enum class TradeOperation : uint8_t { Invite, Accept, Offer, Confirm, Cancel, Query };
enum class TradeStage : uint8_t { Invited, Editing, Committing, Completed, Cancelled };
enum class TradeResult : uint8_t { Success, Invalid, Busy, Unavailable, Stale, Full, StorageError, Expired };
#pragma pack(push, 1)
struct TradeSlot { uint8_t category = 0, slot = 0; uint16_t quantity = 0; };
struct TradeItem { TradeSlot source; uint32_t itemId = 0; uint64_t instanceId = 0; };
struct TradeOffer { uint64_t inventoryRevision = 0, mesos = 0; uint8_t count = 0; TradeItem items[MAX_TRADE_ITEMS]{}; };
struct TradeRequest
{
    TradeOperation operation = TradeOperation::Query;
    uint64_t tradeId = 0, revision = 0;
    uint32_t targetId = 0;
    uint64_t inventoryRevision = 0, mesos = 0;
    uint8_t count = 0; TradeSlot slots[MAX_TRADE_ITEMS]{};
};
struct TradeState
{
    uint64_t tradeId = 0, revision = 1;
    TradeStage stage = TradeStage::Cancelled; TradeResult result = TradeResult::Invalid;
    uint32_t characters[2]{}; uint8_t confirmed[2]{};
    TradeOffer offers[2]{};
};
struct TradeCommitRequest { uint64_t tradeId = 0; uint32_t accounts[2]{}, characters[2]{}; TradeOffer offers[2]{}; };
#pragma pack(pop)
struct TradeCommitResult { TradeResult result = TradeResult::StorageError; InventorySnapshotPacket inventories[2]{}; };
struct TradeLookupResult { TradeState state; InventorySnapshotPacket inventory; bool loaded = false; };
static_assert(sizeof(TradeRequest) == 62);
static_assert(sizeof(TradeState) == 254);
static_assert(sizeof(TradeCommitRequest) == 250);
