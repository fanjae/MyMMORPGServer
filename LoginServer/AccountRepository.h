#pragma once

#include <cstdint>

namespace sql
{
    class Connection;
}

class AccountRepository
{
public:
    explicit AccountRepository(sql::Connection& connection);

    bool ExistsById(uint32_t accountId);

private:
    sql::Connection& _connection;
};