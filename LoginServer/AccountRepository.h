#pragma once

#include "RepositoryResult.h"

#include <cstdint>
#include <string>

namespace sql
{
    class Connection;
}

struct AccountData
{
    uint32_t accountId = 0;
    std::string passwordHash;
};

struct AccountQueryResult
{
    RepositoryStatus status = RepositoryStatus::DatabaseError;
    AccountData account;
};

class AccountRepository
{
public:
    explicit AccountRepository(sql::Connection& connection);

    AccountQueryResult FindByLoginId(const std::string& loginId);

private:
    sql::Connection& _connection;
};