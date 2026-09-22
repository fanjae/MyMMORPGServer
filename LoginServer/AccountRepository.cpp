#include "AccountRepository.h"

#include <mysql/jdbc.h>

#include <iostream>
#include <memory>

AccountRepository::AccountRepository(sql::Connection& connection) : _connection(connection)
{
}

bool AccountRepository::ExistsById(uint32_t accountId)
{
    try
    {
        std::unique_ptr<sql::PreparedStatement> statement(_connection.prepareStatement("SELECT 1 FROM accounts WHERE id = ? LIMIT 1"));
        statement->setUInt(1, accountId);

        std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
        return result->next();
    }
    catch (const sql::SQLException& e)
    {
        std::cerr << "AccountRepository::ExistsById failed: " << e.what() << '\n';
        return false;
    }
}