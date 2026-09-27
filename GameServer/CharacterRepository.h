#pragma once

#include <cstdint>
#include <string>

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

    CharacterLoadResult FindById(uint32_t accountId, uint32_t characterId);

private:
    sql::Connection& _connection;
};