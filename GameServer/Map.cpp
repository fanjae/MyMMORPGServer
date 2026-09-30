#include "Map.h"
#include "GameSession.h"
#include "Monster.h"
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

    bool SendMonsterEnter(GameSession& session, const Monster& monster)
    {
        MonsterEnterMap payload;
        payload.monsterId = monster.GetMonsterId();
        payload.x = monster.GetX();
        payload.y = monster.GetY();

        PacketHeader header;
        header.size = sizeof(PacketHeader) + sizeof(MonsterEnterMap);
        header.opcode = static_cast<uint16_t>(GamePacketOpcode::MonsterEnterMap);

        char sendBuffer[sizeof(PacketHeader) + sizeof(MonsterEnterMap)];
        memcpy(sendBuffer, &header, sizeof(header));
        memcpy(sendBuffer + sizeof(header), &payload, sizeof(payload));
        return session.Send(sendBuffer, sizeof(sendBuffer));
    }

    bool SendPlayerChat(GameSession& session, const Player& player, const char* message)
    {
        PlayerChat payload;
        payload.characterId = player.GetCharacterId();
        strcpy_s(payload.message, message);

        PacketHeader header;
        header.size = sizeof(PacketHeader) + sizeof(PlayerChat);
        header.opcode = static_cast<uint16_t>(GamePacketOpcode::PlayerChat);

        char sendBuffer[sizeof(PacketHeader) + sizeof(PlayerChat)];
        memcpy(sendBuffer, &header, sizeof(header));
        memcpy(sendBuffer + sizeof(header), &payload, sizeof(payload));
        return session.Send(sendBuffer, sizeof(sendBuffer));
    }
}

Map::Map(uint32_t mapId, int32_t spawnX, int32_t spawnY) : _mapId(mapId), _spawnX(spawnX), _spawnY(spawnY)
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

bool Map::MovePlayer(Player& player, int32_t x, int32_t y)
{
    auto it = _players.find(player.GetCharacterId());

    if (it == _players.end() || it->second != &player || player.GetMap() != this)
        return false;

    player.SetPosition(x, y);
    return NotifyPlayerMoved(player);
}

bool Map::AddMonster(Monster& monster)
{
    const uint32_t monsterId = monster.GetMonsterId();

    if (_monsters.find(monsterId) != _monsters.end())
        return false;

    if (monster.GetMap() != nullptr)
        return false;

    _monsters.emplace(monsterId, &monster);
    monster.SetMap(this);
    return true;
}

void Map::RemoveMonster(Monster& monster)
{
    auto it = _monsters.find(monster.GetMonsterId());

    if (it == _monsters.end() || it->second != &monster)
        return;

    _monsters.erase(it);

    if (monster.GetMap() == this)
        monster.SetMap(nullptr);
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

    for (const auto& [monsterId, monster] : _monsters)
    {
        if (!SendMonsterEnter(*enteredSession, *monster))
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

bool Map::NotifyPlayerChat(Player& player, const char* message)
{
    for (const auto& [characterId, existingPlayer] : _players)
    {
        GameSession* existingSession = existingPlayer->GetSession();
        if (existingSession == nullptr)
            continue;

        if (!SendPlayerChat(*existingSession, player, message))
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

Monster* Map::FindMonster(uint32_t monsterId) const
{
    auto it = _monsters.find(monsterId);

    if (it == _monsters.end())
        return nullptr;

    return it->second;
}
