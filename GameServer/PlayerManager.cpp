#include "PlayerManager.h"
#include "Player.h"

bool PlayerManager::Add(Player& player)
{
    const uint32_t characterId = player.GetCharacterId();
    const uint32_t accountId = player.GetAccountId();

    if (_playersByCharacterId.find(characterId) != _playersByCharacterId.end())
        return false;

    if (_playersByAccountId.find(accountId) != _playersByAccountId.end())
        return false;

    _playersByCharacterId.emplace(characterId, &player);
    _playersByAccountId.emplace(accountId, &player);
    return true;
}

void PlayerManager::Remove(Player& player)
{
    const uint32_t characterId = player.GetCharacterId();
    const uint32_t accountId = player.GetAccountId();

    auto characterIt = _playersByCharacterId.find(characterId);
    if (characterIt != _playersByCharacterId.end() && characterIt->second == &player)
        _playersByCharacterId.erase(characterIt);

    auto accountIt = _playersByAccountId.find(accountId);
    if (accountIt != _playersByAccountId.end() && accountIt->second == &player)
        _playersByAccountId.erase(accountIt);
}

Player* PlayerManager::FindByCharacterId(uint32_t characterId) const
{
    auto it = _playersByCharacterId.find(characterId);
    if (it == _playersByCharacterId.end())
        return nullptr;

    return it->second;
}

Player* PlayerManager::FindByAccountId(uint32_t accountId) const
{
    auto it = _playersByAccountId.find(accountId);
    if (it == _playersByAccountId.end())
        return nullptr;

    return it->second;
}
