#include "AccountRepository.h"

#include <mysql/jdbc.h>
#include "../Protocol/RegistrationCredentials.h"

#include <iostream>
#include <memory>
#include <stdexcept>

AccountRepository::AccountRepository(sql::Connection& connection) : _connection(connection)
{
}

RegistrationResult AccountRepository::CreateWithCharacter(const std::string& loginId, const std::string& passwordHash)
{
    if (!IsValidRegistrationId(loginId) || passwordHash.empty() || passwordHash.size() > 255) return RegistrationResult::ServerError;
    try
    {
        _connection.setAutoCommit(false);
        struct Transaction
        {
            sql::Connection& connection;
            ~Transaction() { try { connection.rollback(); connection.setAutoCommit(true); } catch (...) {} }
        } transaction{_connection};
        std::unique_ptr<sql::PreparedStatement> account(_connection.prepareStatement("INSERT INTO accounts(login_id,password_hash) VALUES(?,?)"));
        account->setString(1, loginId); account->setString(2, passwordHash); account->executeUpdate();
        auto inserted = [&]()
        {
            std::unique_ptr<sql::Statement> statement(_connection.createStatement());
            std::unique_ptr<sql::ResultSet> row(statement->executeQuery("SELECT LAST_INSERT_ID()"));
            if (!row->next()) throw std::runtime_error("Missing inserted ID");
            auto id = row->getUInt64(1);
            if (!id || id > UINT32_MAX) throw std::runtime_error("Inserted ID outside protocol range");
            return static_cast<uint32_t>(id);
        };
        const auto accountId = inserted();
        std::unique_ptr<sql::PreparedStatement> character(_connection.prepareStatement("INSERT INTO characters(account_id,name,level) VALUES(?,'Player',1)"));
        character->setUInt(1, accountId); character->executeUpdate();
        const auto characterId = inserted();
        std::unique_ptr<sql::PreparedStatement> name(_connection.prepareStatement("UPDATE characters SET name=? WHERE id=?"));
        name->setString(1, "Player" + std::to_string(characterId)); name->setUInt(2, characterId); name->executeUpdate();
        // 기본 캐릭터를 만들지 못하면 계정도 남기지 않고 함께 되돌린다.
        _connection.commit();
        return RegistrationResult::Success;
    }
    catch (const sql::SQLException& error)
    {
        if (error.getErrorCode() == 1062) return RegistrationResult::DuplicateId;
        std::cerr << "Account registration storage error: " << error.getErrorCode() << '\n';
    }
    catch (const std::exception&) { std::cerr << "Account registration failed\n"; }
    return RegistrationResult::ServerError;
}

AccountQueryResult AccountRepository::FindByLoginId(const std::string& loginId)
{
    AccountQueryResult queryResult;

    try
    {
        std::unique_ptr<sql::PreparedStatement> statement(_connection.prepareStatement("SELECT id, password_hash " "FROM accounts " "WHERE login_id = ? " "LIMIT 1"));

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
