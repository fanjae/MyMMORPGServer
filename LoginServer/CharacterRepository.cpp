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

std::vector<CharacterInfo> CharacterRepository::FindByAccountId(uint32_t accountId)
{
    std::vector<CharacterInfo> characters;

    try
    {
        std::unique_ptr<sql::PreparedStatement> statement(_connection.prepareStatement(
                "SELECT id, name, level "
                "FROM characters "
                "WHERE account_id = ? "
                "ORDER BY id"
        ));

        statement->setUInt(1, accountId);

        std::unique_ptr<sql::ResultSet> result(statement->executeQuery());

        while (result->next())
        {
            CharacterInfo character;

            character.characterId = result->getUInt("id");

            const std::string name = result->getString("name");
            const size_t copyLength = std::min(name.size(), static_cast<size_t>(MAX_CHARACTER_NAME_LENGTH - 1));
            memcpy(character.name, name.data(), copyLength);

            character.level = static_cast<uint16_t>(result->getUInt("level"));

            characters.push_back(character);
        }
    }
    catch (const sql::SQLException& e)
    {
        std::cerr << "FindByAccountId failed: " << e.what() << '\n';
    }

    return characters;
}

bool CharacterRepository::ExistsByAccountIdAndCharacterId(uint32_t accountId, uint32_t characterId)
{
    try
    {
        std::unique_ptr<sql::PreparedStatement> statement(
            _connection.prepareStatement(
                "SELECT 1 "
                "FROM characters "
                "WHERE account_id = ? AND id = ? "
                "LIMIT 1"));

        statement->setUInt(1, accountId);
        statement->setUInt(2, characterId);

        std::unique_ptr<sql::ResultSet> result(statement->executeQuery());

        return result->next();
    }
    catch (const sql::SQLException& e)
    {
        std::cerr << "ExistsByAccountIdAndCharacterId failed: " << e.what() << '\n';
        return false;
    }
}