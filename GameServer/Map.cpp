#include "Map.h"
#include "Player.h"

Map::Map(uint32_t mapId) : _mapId(mapId)
{
}

bool Map::AddPlayer(Player& player)
{
    const uint32_t characterId = player.GetCharacterId();

    if (_players.find(characterId) != _players.end())
        return false;

    if (player.GetMap() != nullptr)
        return false;

    _players.emplace(characterId, &player);
    player.SetMap(this);
    return true;
}

void Map::RemovePlayer(Player& player)
{
    auto it = _players.find(player.GetCharacterId());

    if (it == _players.end() || it->second != &player)
        return;

    _players.erase(it);

    if (player.GetMap() == this)
        player.SetMap(nullptr);
}

Player* Map::FindPlayer(uint32_t characterId) const
{
    auto it = _players.find(characterId);

    if (it == _players.end())
        return nullptr;

    return it->second;
}
