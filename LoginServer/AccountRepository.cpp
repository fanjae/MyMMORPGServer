#include "AccountRepository.h"

#include <mysql/jdbc.h>

#include <iostream>
#include <memory>

AccountRepository::AccountRepository(sql::Connection& connection) : _connection(connection)
{
}

AccountQueryResult AccountRepository::FindByLoginId(const std::string& loginId)
{
    AccountQueryResult queryResult;

    try
    {
        std::unique_ptr<sql::PreparedStatement> statement(_connection.prepareStatement(
            "SELECT id, password_hash "
            "FROM accounts "
            "WHERE login_id = ? "
            "LIMIT 1"));

        statement->setString(1, loginId);

        std::unique_ptr<sql::ResultSet> result(statement->executeQuery());

        if (!result->next())
        {
            queryResult.status = RepositoryStatus::NotFound;
            return queryResult;
        }

        queryResult.account.accountId = result->getUInt("id");
        queryResult.account.passwordHash = result->getString("password_hash");
        queryResult.status = RepositoryStatus::Success;
        return queryResult;
    }
    catch (const sql::SQLException& e)
    {
        std::cerr << "AccountRepository::FindByLoginId failed: " << e.what() << '\n';
        queryResult.status = RepositoryStatus::DatabaseError;
        return queryResult;
    }
}