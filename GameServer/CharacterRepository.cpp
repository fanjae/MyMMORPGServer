#include "CharacterRepository.h"

#include <mysql/jdbc.h>
#include "../Protocol/CharacterName.h"

#include <iostream>
#include <memory>

CharacterRepository::CharacterRepository(sql::Connection& connection)
    : _connection(connection), _worker([this] { Run(); })
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
        _jobs.push(std::move(job));
    }
    _ready.notify_one();
    return result;
}

void CharacterRepository::Run()
{
    sql::Driver* driver = sql::mysql::get_driver_instance();
    driver->threadInit();
    while (true)
    {
        std::packaged_task<CharacterLoadResult()> job;
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

CharacterLoadResult CharacterRepository::FindById(uint32_t accountId, uint32_t characterId)
{
    CharacterLoadResult queryResult;

    try
    {
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
        std::cerr << "CharacterRepository::FindById failed: " << e.what() << '\n';
        queryResult.status = CharacterLoadStatus::DatabaseError;
        return queryResult;
    }
}
