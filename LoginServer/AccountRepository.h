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

// 로그인 계정 조회를 담당하는 Repository.
// DB Connection을 소유하지 않으므로 Repository보다 Connection이 오래 유지되어야 한다.
class AccountRepository
{
public:
    explicit AccountRepository(sql::Connection& connection);

    AccountQueryResult FindByLoginId(const std::string& loginId);

private:
    sql::Connection& _connection;
};