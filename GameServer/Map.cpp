#include "Map.h"
#include "../ServerCore/Session.h"
#include "Monster.h"
#include "Player.h"
#include "../Protocol/GamePacket.h"
#include "../ServerCore/Packet.h"

#include <cstring>
#include <cmath>
#include <vector>

namespace
{
    template<typename T>
    bool SendPacket(Session& session, GamePacketOpcode opcode, const T& payload)
    {
        static_assert(sizeof(T) + sizeof(PacketHeader) <= MAX_PACKET_SIZE);
        PacketHeader header;
        header.size = sizeof(PacketHeader) + sizeof(T);
        header.opcode = static_cast<uint16_t>(opcode);
        char buffer[sizeof(PacketHeader) + sizeof(T)];
        memcpy(buffer, &header, sizeof(header));
        memcpy(buffer + sizeof(header), &payload, sizeof(payload));
        return session.Send(buffer, sizeof(buffer));
    }

    bool SendPlayerEnter(Session& session, const Player& player)
    {
        PlayerEnterMap payload;
        payload.characterId = player.GetCharacterId();
        if (!CopyCharacterName(payload.name, player.GetName()))
            return false;
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

    bool SendPlayerMove(Session& session, const Player& player)
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

    bool SendPlayerLeave(Session& session, uint32_t characterId)
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

    bool SendMonsterEnter(Session& session, const Monster& monster)
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

    bool SendPlayerChat(Session& session, const Player& player, const char* message)
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

Map::Map(const MapDefinition& definition) : _definition(definition)
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
    player.GetMovementValidator().Reset(_definition.moveSpeed, _definition.moveBurst);
    player.BeginMap();
    if (IsPlatformer())
    {
        _simulation->Reset(player.GetPlatformState());
        player.SetPosition(_definition.spawnX, _definition.spawnY);
    }
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

MoveResult Map::MovePlayer(Player& player, int32_t x, int32_t y)
{
    if (IsPlatformer())
        return MoveResult::WrongMovementMode;
    auto it = _players.find(player.GetCharacterId());

    if (it == _players.end() || it->second != &player || player.GetMap() != this)
        return MoveResult::MapMismatch;

    if (!_definition.Contains(x, y))
        return MoveResult::OutOfBounds;

    // int32 좌표끼리 빼기 전에 변환해 극단적인 요청 좌표에서도 overflow를 피한다.
    double deltaX = static_cast<double>(x) - player.GetX();
    double deltaY = static_cast<double>(y) - player.GetY();
    double distance = std::hypot(deltaX, deltaY);

    if (!player.GetMovementValidator().TryConsume(distance))
        return MoveResult::SpeedExceeded;

    player.SetPosition(x, y);
    return MoveResult::Success;
}

bool Map::SendMapInfo(Player& player)
{
    Session* session = player.GetSession();
    if (session == nullptr)
        return false;

    MapInfo payload;
    payload.mapId = _definition.mapId;
    payload.minX = _definition.minX;
    payload.maxX = _definition.maxX;
    payload.minY = _definition.minY;
    payload.maxY = _definition.maxY;
    payload.moveSpeed = _definition.moveSpeed;
    payload.moveBurst = _definition.moveBurst;

    PacketHeader header;
    header.size = sizeof(PacketHeader) + sizeof(MapInfo);
    header.opcode = static_cast<uint16_t>(GamePacketOpcode::MapInfo);

    char sendBuffer[sizeof(PacketHeader) + sizeof(MapInfo)];
    memcpy(sendBuffer, &header, sizeof(header));
    memcpy(sendBuffer + sizeof(header), &payload, sizeof(payload));
    return session->Send(sendBuffer, sizeof(sendBuffer));
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
    Session* enteredSession = player.GetSession();
    if (enteredSession == nullptr)
        return false;

    for (const auto& [characterId, existingPlayer] : _players)
    {
        if (existingPlayer == &player)
            continue;

        Session* existingSession = existingPlayer->GetSession();
        if (existingSession == nullptr || !existingSession->IsConnected())
            continue;

        if (!SendPlayerEnter(*enteredSession, *existingPlayer))
            return false;

        if (IsPlatformer() && !SendMovementState(player, *existingPlayer))
            return false;

        // 기존 Player의 전송 실패는 해당 연결에만 적용하고 입장자의 전송은 계속한다.
        if (!SendPlayerEnter(*existingSession, player) || (IsPlatformer() && !SendMovementState(*existingPlayer, player)))
            existingSession->RequestClose();
    }

    for (const auto& [monsterId, monster] : _monsters)
    {
        if (!SendMonsterEnter(*enteredSession, *monster))
            return false;
    }

    return true;
}

void Map::SetGeometry(const MapGeometry& geometry)
{
    _geometry = &geometry;
    _simulation = std::make_unique<MovementSimulation>(geometry);
}

bool Map::IsPlatformer() const
{
    return _geometry != nullptr && _geometry->movement.movementMode == MovementMode::Platformer;
}

bool Map::SendGeometry(Player& player)
{
    Session* session = player.GetSession();
    if (_geometry == nullptr || session == nullptr)
        return false;

    const auto& movement = _geometry->movement;
    MapGeometryPacket payload;
    payload.mapId = GetMapId();
    payload.generation = player.GetGeneration();
    payload.version = movement.geometryVersion;
    payload.mode = static_cast<uint8_t>(movement.movementMode);
    payload.halfWidth = movement.halfWidth;
    payload.halfHeight = movement.halfHeight;
    payload.horizontalSpeed = movement.horizontalSpeed;
    payload.jumpSpeed = movement.jumpSpeed;
    payload.gravity = movement.gravity;
    payload.maxFallSpeed = movement.maxFallSpeed;
    payload.spawnX = GetSpawnX();
    payload.spawnY = GetSpawnY();
    payload.spawnFootholdId = movement.spawnFootholdId;
    payload.footholdCount = static_cast<uint16_t>(_geometry->footholds.size());
    payload.colliderCount = static_cast<uint16_t>(_geometry->colliders.size());
    if (!SendPacket(*session, GamePacketOpcode::MapGeometry, payload))
        return false;

    // 지형을 객체 단위로 나눠 전송해 최대 패킷 크기를 넘지 않도록 한다.
    for (const auto& foothold : _geometry->footholds)
    {
        FootholdPacket packet{ GetMapId(), player.GetGeneration(), foothold.footholdId, foothold.x1, foothold.y1, foothold.x2, foothold.y2, foothold.prevId, foothold.nextId };
        if (!SendPacket(*session, GamePacketOpcode::Foothold, packet))
            return false;
    }

    for (const auto& collider : _geometry->colliders)
    {
        ColliderPacket packet{ GetMapId(), player.GetGeneration(), collider.colliderId, collider.minX, collider.minY, collider.maxX, collider.maxY };
        if (!SendPacket(*session, GamePacketOpcode::Collider, packet))
            return false;
    }

    GeometryEndPacket end{ GetMapId(), player.GetGeneration() };
    if (!SendPacket(*session, GamePacketOpcode::GeometryEnd, end))
        return false;

    return !IsPlatformer() || SendMovementState(player, player);
}

bool Map::SendMovementState(Player& recipient, const Player& player, MovementStateReason reason)
{
    if (recipient.GetSession() == nullptr)
        return false;

    const auto& state = player.GetPlatformState();
    MovementStatePacket packet;
    packet.mapId = GetMapId();
    // 수신자의 입장 번호를 사용해 같은 맵 재입장 전의 상태도 구분한다.
    packet.generation = recipient.GetGeneration();
    packet.characterId = player.GetCharacterId();
    packet.serverTick = _tick;
    packet.sequence = player.GetInputSequence();
    packet.x = std::llround(state.x * 1000.0);
    packet.y = std::llround(state.y * 1000.0);
    packet.velocityX = std::llround(state.velocityX * 1000.0);
    packet.velocityY = std::llround(state.velocityY * 1000.0);
    packet.footholdId = state.footholdId;
    packet.grounded = state.grounded;
    packet.reason = reason;
    packet.inputTicks = player.GetInputTicks();
    packet.jumpHeld = state.jumpHeld;
    return SendPacket(*recipient.GetSession(), GamePacketOpcode::MovementState, packet);
}

void Map::Tick(uint64_t tick, std::chrono::steady_clock::time_point now)
{
    _tick = tick;
    if (!IsPlatformer())
        return;

    std::vector<Session*> failed;
    for (const auto& entry : _players)
    {
        Player& player = *entry.second;
        auto& state = player.GetPlatformState();
        bool grounded = state.grounded;
        bool expired = false;
        bool hadInput = state.velocityX != 0.0 || state.jumpHeld;
        auto input = player.ConsumeInput(now, expired);
        SimulationResult result = _simulation->Step(state, input);
        player.FinishInput();
        MovementStateReason reason = MovementStateReason::Normal;
        if (result == SimulationResult::InvalidState)
        {
            _simulation->Reset(state);
            reason = MovementStateReason::Respawned;
        }
        else if (result == SimulationResult::Respawned)
            reason = MovementStateReason::Respawned;
        else if (expired && hadInput)
            reason = MovementStateReason::InputExpired;

        player.SetPosition(static_cast<int32_t>(std::llround(state.x)), static_cast<int32_t>(std::llround(state.y)));
        if (tick % 3 != 0 && grounded == state.grounded && reason == MovementStateReason::Normal)
            continue;

        for (const auto& recipient : _players)
        {
            if (!SendMovementState(*recipient.second, player, reason))
                failed.push_back(recipient.second->GetSession());
        }
    }

    // 연결 종료 콜백은 Player를 제거하므로 SessionManager에서 처리하도록 예약한다.
    for (Session* session : failed)
    {
        if (session != nullptr)
            session->RequestClose();
    }
}

void Map::NotifyPlayerLeaving(Player& player)
{
    for (const auto& [characterId, existingPlayer] : _players)
    {
        if (existingPlayer == &player)
            continue;

        Session* existingSession = existingPlayer->GetSession();
        if (existingSession == nullptr)
            continue;

        if (!SendPlayerLeave(*existingSession, player.GetCharacterId()))
            existingSession->RequestClose();
    }
}

bool Map::NotifyPlayerMoved(Player& player)
{
    for (const auto& [characterId, existingPlayer] : _players)
    {
        if (existingPlayer == &player)
            continue;

        Session* existingSession = existingPlayer->GetSession();
        if (existingSession == nullptr)
            continue;

        if (!SendPlayerMove(*existingSession, player))
            existingSession->RequestClose();
    }

    return true;
}

bool Map::NotifyPlayerChat(Player& player, const char* message)
{
    for (const auto& [characterId, existingPlayer] : _players)
    {
        Session* existingSession = existingPlayer->GetSession();
        if (existingSession == nullptr)
            continue;

        if (!SendPlayerChat(*existingSession, player, message))
            existingSession->RequestClose();
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
