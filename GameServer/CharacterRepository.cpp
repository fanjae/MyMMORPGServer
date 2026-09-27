#include "CharacterRepository.h"

#include <mysql/jdbc.h>

#include <iostream>
#include <memory>

CharacterRepository::CharacterRepository(sql::Connection& connection)
    : _connection(connection)
{
}

CharacterLoadResult CharacterRepository::FindById(uint32_t accountId, uint32_t characterId)
{
    CharacterLoadResult queryResult;

    try
    {
        std::unique_ptr<sql::PreparedStatement> statement(_connection.prepareStatement(
            "SELECT id, account_id, name, level "
            "FROM characters "
            "WHERE account_id = ? AND id = ? "
            "LIMIT 1"));

        statement->setUInt(1, accountId);
        statement->setUInt(2, characterId);

        std::unique_ptr<sql::ResultSet> result(statement->executeQuery());

        if (!result->next())
        {
            queryResult.status = CharacterLoadStatus::NotFound;
            return queryResult;
        }

        queryResult.character.characterId = result->getUInt("id");
        queryResult.character.accountId = result->getUInt("account_id");
        queryResult.character.name = result->getString("name");
        queryResult.character.level = static_cast<uint16_t>(result->getUInt("level"));
        queryResult.status = CharacterLoadStatus::Success;

        return queryResult;
    }
    catch (const sql::SQLException& e)
    {
        std::cerr << "CharacterRepository::FindById failed: " << e.what() << '\n';
        queryResult.status = CharacterLoadStatus::DatabaseError;
        return queryResult;
    }
}