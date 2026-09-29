#include "Map.h"
#include "GameSession.h"
#include "Player.h"
#include "../Protocol/GamePacket.h"
#include "../ServerCore/Packet.h"

#include <cstring>

namespace
{
    bool SendPlayerEnter(GameSession& session, const Player& player)
    {
        PlayerEnterMap payload;
        payload.characterId = player.GetCharacterId();
        strcpy_s(payload.name, player.GetName().c_str());
        payload.level = player.GetLevel();
        payload.x = player.GetX();
        payload.y = player.GetY();

        PacketHeader header;
        header.size = sizeof(PacketHeader) + sizeof(PlayerEnterMap);
        header.opcode = static_cast<uint16_t>(GamePacketOpcode::PlayerEnterMap);

        char sendBuffer[sizeof(PacketHeader) + sizeof(PlayerEnterMap)];
        memcpy(sendBuffer, &header, sizeof(header));
        memcpy(sendBuffer + sizeof(header), &payload, sizeof(payload));
        return session.Send(sendBuffer, sizeof(sendBuffer));
    }

    bool SendPlayerMove(GameSession& session, const Player& player)
    {
        PlayerMove payload;
        payload.characterId = player.GetCharacterId();
        payload.x = player.GetX();
        payload.y = player.GetY();

        PacketHeader header;
        header.size = sizeof(PacketHeader) + sizeof(PlayerMove);
        header.opcode = static_cast<uint16_t>(GamePacketOpcode::PlayerMove);

        char sendBuffer[sizeof(PacketHeader) + sizeof(PlayerMove)];
        memcpy(sendBuffer, &header, sizeof(header));
        memcpy(sendBuffer + sizeof(header), &payload, sizeof(payload));
        return session.Send(sendBuffer, sizeof(sendBuffer));
    }

    bool SendPlayerLeave(GameSession& session, uint32_t characterId)
    {
        PlayerLeaveMap payload;
        payload.characterId = characterId;

        PacketHeader header;
        header.size = sizeof(PacketHeader) + sizeof(PlayerLeaveMap);
        header.opcode = static_cast<uint16_t>(GamePacketOpcode::PlayerLeaveMap);

        char sendBuffer[sizeof(PacketHeader) + sizeof(PlayerLeaveMap)];
        memcpy(sendBuffer, &header, sizeof(header));
        memcpy(sendBuffer + sizeof(header), &payload, sizeof(payload));
        return session.Send(sendBuffer, sizeof(sendBuffer));
    }
}

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

bool Map::NotifyPlayerEntered(Player& player)
{
    GameSession* enteredSession = player.GetSession();
    if (enteredSession == nullptr)
        return false;

    for (const auto& [characterId, existingPlayer] : _players)
    {
        if (existingPlayer == &player)
            continue;

        GameSession* existingSession = existingPlayer->GetSession();
        if (existingSession == nullptr)
            continue;

        if (!SendPlayerEnter(*enteredSession, *existingPlayer))
            return false;

        if (!SendPlayerEnter(*existingSession, player))
            return false;
    }

    return true;
}

void Map::NotifyPlayerLeaving(Player& player)
{
    for (const auto& [characterId, existingPlayer] : _players)
    {
        if (existingPlayer == &player)
            continue;

        GameSession* existingSession = existingPlayer->GetSession();
        if (existingSession == nullptr)
            continue;

        SendPlayerLeave(*existingSession, player.GetCharacterId());
    }
}

bool Map::NotifyPlayerMoved(Player& player)
{
    for (const auto& [characterId, existingPlayer] : _players)
    {
        if (existingPlayer == &player)
            continue;

        GameSession* existingSession = existingPlayer->GetSession();
        if (existingSession == nullptr)
            continue;

        if (!SendPlayerMove(*existingSession, player))
            return false;
    }

    return true;
}

Player* Map::FindPlayer(uint32_t characterId) const
{
    auto it = _players.find(characterId);

    if (it == _players.end())
        return nullptr;

    return it->second;
}
