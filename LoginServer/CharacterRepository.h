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

// 캐릭터 조회를 담당하는 Repository.
// DB Connection을 소유하지 않으므로 Repository보다 Connection이 오래 유지 필요
class CharacterRepository
{
public:
    explicit CharacterRepository(sql::Connection& connection);

    CharacterListQueryResult FindByAccountId(uint32_t accountId);
    RepositoryStatus ExistsByAccountIdAndCharacterId(uint32_t accountId, uint32_t characterId);

private:
    sql::Connection& _connection;
};