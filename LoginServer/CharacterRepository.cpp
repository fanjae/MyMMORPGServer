#include "CharacterRepository.h"

#include <mysql/jdbc.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>

CharacterRepository::CharacterRepository(sql::Connection& connection)
    : _connection(connection)
{
}

CharacterListQueryResult CharacterRepository::FindByAccountId(uint32_t accountId)
{
    CharacterListQueryResult queryResult;

    try
    {
        std::unique_ptr<sql::PreparedStatement> statement(_connection.prepareStatement(
            "SELECT id, name, level "
            "FROM characters "
            "WHERE account_id = ? "
            "ORDER BY id"));

        statement->setUInt(1, accountId);

        std::unique_ptr<sql::ResultSet> result(statement->executeQuery());

        while (result->next())
        {
            CharacterInfo character;

            character.characterId = result->getUInt("id");

            // 프로토콜의 고정 길이 name 버퍼에 맞춰 마지막 null 문자를 위한
            // 1바이트를 남기고 복사한다. CharacterInfo가 zero-initialize되어 있으므로
            // 복사되지 않은 나머지 영역은 null로 유지된다.
            const std::string name = result->getString("name");
            const size_t copyLength = (std::min)(name.size(), static_cast<size_t>(MAX_CHARACTER_NAME_LENGTH - 1));
            memcpy(character.name, name.data(), copyLength);

            character.level = static_cast<uint16_t>(result->getUInt("level"));

            queryResult.characters.push_back(character);
        }

        queryResult.status = RepositoryStatus::Success;
        return queryResult;
    }
    catch (const sql::SQLException& e)
    {
        std::cerr << "CharacterRepository::FindByAccountId failed: " << e.what() << '\n';
        queryResult.status = RepositoryStatus::DatabaseError;
        return queryResult;
    }
}

RepositoryStatus CharacterRepository::ExistsByAccountIdAndCharacterId(uint32_t accountId, uint32_t characterId)
{
    try
    {
        std::unique_ptr<sql::PreparedStatement> statement(_connection.prepareStatement(
            "SELECT 1 "
            "FROM characters "
            "WHERE account_id = ? AND id = ? "
            "LIMIT 1"));

        statement->setUInt(1, accountId);
        statement->setUInt(2, characterId);

        std::unique_ptr<sql::ResultSet> result(statement->executeQuery());

        if (!result->next())
            return RepositoryStatus::NotFound;

        return RepositoryStatus::Success;
    }
    catch (const sql::SQLException& e)
    {
        std::cerr << "CharacterRepository::ExistsByAccountIdAndCharacterId failed: " << e.what() << '\n';
        return RepositoryStatus::DatabaseError;
    }
}