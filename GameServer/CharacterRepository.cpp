#include "CharacterRepository.h"

#include <mysql/jdbc.h>
#include "InventoryRepository.h"
#include "../Protocol/CharacterName.h"

#include <iostream>
#include <memory>

CharacterRepository::CharacterRepository(sql::Connection& connection)
    : _connection(connection), _inventory(std::make_unique<InventoryRepository>(connection)), _worker([this] { Run(); })
{
}

CharacterRepository::~CharacterRepository()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stopping = true;
        while (!_jobs.empty())
            _jobs.pop();
    }
    _ready.notify_one();
    _worker.join();
}

std::future<CharacterLoadResult> CharacterRepository::FindByIdAsync(uint32_t accountId, uint32_t characterId, std::shared_ptr<std::atomic<bool>> canceled)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::packaged_task<CharacterLoadResult()> job([this, accountId, characterId, canceled, deadline]
    {
        // 대기 중 연결이 종료되거나 입장 제한 시간을 넘기면 DB 조회를 시작하지 않는다.
        if (canceled->load() || std::chrono::steady_clock::now() >= deadline)
            return CharacterLoadResult{};
        return FindById(accountId, characterId);
    });
    auto result = job.get_future();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_stopping || _jobs.size() >= 64)
            return {};
        _jobs.emplace([job = std::move(job)]() mutable { job(); });
    }
    _ready.notify_one();
    return result;
}

std::future<InventorySnapshotPacket> CharacterRepository::ApplyInventoryAsync(uint32_t accountId, uint32_t characterId,
    InventoryRequestPacket request, std::shared_ptr<std::atomic<bool>> canceled, InventoryMutation mutation)
{
    // DB 잠금 대기는 작업 스레드에 맡기고 호출자는 완료 여부만 확인한다.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::packaged_task<InventorySnapshotPacket()> job([this, accountId, characterId, request, canceled, deadline, mutation]
    { return _inventory->Apply(accountId, characterId, request, canceled, deadline, mutation); });
    auto result = job.get_future();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        // 캐릭터 조회와 같은 대기열 한도를 적용해 DB 요청이 무한히 쌓이지 않도록 한다.
        if (_stopping || _jobs.size() >= 64) return {};
        _jobs.emplace([job = std::move(job)]() mutable { job(); });
    }
    _ready.notify_one(); return result;
}

void CharacterRepository::Run()
{
    sql::Driver* driver = sql::mysql::get_driver_instance();
    driver->threadInit();
    while (true)
    {
        std::packaged_task<void()> job;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _ready.wait(lock, [this] { return _stopping || !_jobs.empty(); });
            if (_stopping)
                break;
            job = std::move(_jobs.front());
            _jobs.pop();
        }

        // 공유 Connection은 이 작업 스레드에서만 순서대로 사용하고 Map tick은 계속 진행한다.
        job();
    }
    driver->threadEnd();
}

std::future<TradeCommitResult> CharacterRepository::ExchangeAsync(TradeCommitRequest request)
{
    std::packaged_task<TradeCommitResult()> job([this,request] { return _inventory->Exchange(request); });
    auto result=job.get_future();
    { std::lock_guard<std::mutex> lock(_mutex); if(_stopping || _jobs.size()>=64) return {}; _jobs.emplace([job=std::move(job)]() mutable { job(); }); }
    _ready.notify_one(); return result;
}
std::future<TradeLookupResult> CharacterRepository::LookupTradeAsync(uint32_t account,uint32_t character,uint64_t tradeId)
{
    std::packaged_task<TradeLookupResult()> job([this,account,character,tradeId] { return _inventory->LookupTrade(account,character,tradeId); });
    auto result=job.get_future();
    { std::lock_guard<std::mutex> lock(_mutex); if(_stopping || _jobs.size()>=64) return {}; _jobs.emplace([job=std::move(job)]() mutable { job(); }); }
    _ready.notify_one(); return result;
}

CharacterLoadResult CharacterRepository::FindById(uint32_t accountId, uint32_t characterId)
{
    CharacterLoadResult queryResult;

    try
    {
        // 인벤토리와 공유하는 연결이 끊겼다면 새 입장 조회에서도 복구할 수 있도록 한다.
        if (!_inventory->TryReconnect()) return queryResult;
        std::unique_ptr<sql::PreparedStatement> statement(_connection.prepareStatement(
            "SELECT id, account_id, name, level "
            "FROM characters "
            "WHERE account_id = ? AND id = ? "
            "LIMIT 1"));

        statement->setUInt(1, accountId);
        statement->setUInt(2, characterId);

        std::unique_ptr<sql::ResultSet> result(statement->executeQuery());

        if (!result->next())
        {
            queryResult.status = CharacterLoadStatus::NotFound;
            return queryResult;
        }

        queryResult.character.characterId = result->getUInt("id");
        queryResult.character.accountId = result->getUInt("account_id");
        queryResult.character.name = result->getString("name");
        if (!IsValidCharacterName(queryResult.character.name))
        {
            queryResult.status = CharacterLoadStatus::DatabaseError;
            return queryResult;
        }
        queryResult.character.level = static_cast<uint16_t>(result->getUInt("level"));
        queryResult.status = CharacterLoadStatus::Success;

        return queryResult;
    }
    catch (const sql::SQLException& e)
    {
        _inventory->ConnectionFailed(e.getErrorCode(), e.getSQLState());
        std::cerr << "CharacterRepository::FindById failed: " << e.what() << '\n';
        queryResult.status = CharacterLoadStatus::DatabaseError;
        return queryResult;
    }
}
