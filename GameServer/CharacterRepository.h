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

private:
    CharacterLoadResult FindById(uint32_t accountId, uint32_t characterId);
    void Run();

    sql::Connection& _connection;
    std::mutex _mutex;
    std::condition_variable _ready;
    std::queue<std::packaged_task<CharacterLoadResult()>> _jobs;
    bool _stopping = false;
    std::thread _worker;
};
