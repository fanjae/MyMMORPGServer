#pragma once

#include "../Protocol/LoginPacket.h"
#include "RepositoryResult.h"

#include <cstdint>
#include <vector>

namespace sql
{
    class Connection;
}

struct CharacterListQueryResult
{
    RepositoryStatus status = RepositoryStatus::DatabaseError;
    std::vector<CharacterInfo> characters;
};

class CharacterRepository
{
public:
    explicit CharacterRepository(sql::Connection& connection);

    CharacterListQueryResult FindByAccountId(uint32_t accountId);
    RepositoryStatus ExistsByAccountIdAndCharacterId(uint32_t accountId, uint32_t characterId);

private:
    sql::Connection& _connection;
};