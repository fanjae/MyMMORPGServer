#pragma once

#include <cstdint>
#include <string>
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <memory>
#include "../Protocol/FieldPacket.h"
#include "../Protocol/TradePacket.h"

namespace sql
{
    class Connection;
}

enum class CharacterLoadStatus
{
    Success,
    NotFound,
    DatabaseError
};

class InventoryRepository;

struct CharacterData
{
    uint32_t characterId = 0;
    uint32_t accountId = 0;
    std::string name;
    uint16_t level = 0;
};

struct CharacterLoadResult
{
    CharacterLoadStatus status = CharacterLoadStatus::DatabaseError;
    CharacterData character;
};

class CharacterRepository
{
public:
    explicit CharacterRepository(sql::Connection& connection);
    ~CharacterRepository();

    std::future<CharacterLoadResult> FindByIdAsync(uint32_t accountId, uint32_t characterId, std::shared_ptr<std::atomic<bool>> canceled);
    std::future<TradeCommitResult> ExchangeAsync(TradeCommitRequest request);
    std::future<TradeLookupResult> LookupTradeAsync(uint32_t account,uint32_t character,uint64_t tradeId);
    std::future<InventorySnapshotPacket> ApplyInventoryAsync(uint32_t accountId, uint32_t characterId,
        InventoryRequestPacket request, std::shared_ptr<std::atomic<bool>> canceled, InventoryMutation mutation = {});

private:
    CharacterLoadResult FindById(uint32_t accountId, uint32_t characterId);
    void Run();

    sql::Connection& _connection;
    std::mutex _mutex;
    std::condition_variable _ready;
    // 반환 형식이 다른 조회·인벤토리 작업도 하나의 Connection에서 순서대로 실행한다.
    std::queue<std::packaged_task<void()>> _jobs;
    bool _stopping = false;
    std::unique_ptr<InventoryRepository> _inventory;
    std::thread _worker;
};
