#pragma once

#include "LoginPacket.h"

#include <cstdint>
#include <vector>

namespace sql
{
    class Connection;
}

class CharacterRepository
{
public:
    explicit CharacterRepository(sql::Connection& connection);

    std::vector<CharacterInfo> FindByAccountId(uint32_t accountId);
    bool ExistsByAccountIdAndCharacterId(uint32_t accountId, uint32_t characterId);

private:
    sql::Connection& _connection;
};