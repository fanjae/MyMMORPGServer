#include "Map.h"
#include "../ServerCore/Session.h"
#include "Monster.h"
#include "Player.h"
#include "../Protocol/GamePacket.h"
#include "../ServerCore/Packet.h"

#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

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
        // 초기 지형 상태만 설정한다. 이동·중력·착지 계산은 클라이언트가 수행한다.
        auto& state = player.GetPlatformState();
        state.x = _definition.spawnX;
        state.y = _definition.spawnY;
        state.footholdId = _geometry->movement.spawnFootholdId;
        state.grounded = true;
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
    _pendingActions.erase(player.GetCharacterId());
    // 퇴장한 캐릭터의 이전 맵 행동을 나중에 중계하지 않는다.
    for (auto& entry : _pendingActions)
    {
        auto& queue = entry.second;
        queue.erase(std::remove_if(queue.begin(), queue.end(), [&](const PendingAction& item)
            { return item.relay.characterId == player.GetCharacterId(); }), queue.end());
    }

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
    packet.serverTick = player.GetActionTick();
    packet.sequence = player.GetActionSequence();
    packet.x = std::llround(state.x * 1000.0);
    packet.y = std::llround(state.y * 1000.0);
    packet.velocityX = std::llround(state.velocityX * 1000.0);
    packet.velocityY = std::llround(state.velocityY * 1000.0);
    packet.footholdId = state.footholdId;
    packet.grounded = state.grounded;
    packet.reason = reason;
    packet.inputTicks = 0; // 초기 상태 형식만 유지하며 서버 물리 적용 단계는 없다.
    packet.jumpHeld = state.jumpHeld;
    return SendPacket(*recipient.GetSession(), GamePacketOpcode::MovementState, packet);
}

void Map::Tick(uint64_t tick, std::chrono::steady_clock::time_point now)
{
    _tick = tick;
    if (!IsPlatformer())
        return;
    std::vector<Session*> failed;
    constexpr size_t maxRelayCount = (MAX_PACKET_SIZE - sizeof(PacketHeader) - sizeof(MovementActionsBroadcastHeader)) / sizeof(RelayedMovementAction);
    for (const auto& entry : _players)
    {
        Player& recipient = *entry.second;
        auto found = _pendingActions.find(recipient.GetCharacterId());
        if (found == _pendingActions.end() || found->second.empty() || !recipient.CanSendMovement(now))
            continue;
        auto& queue = found->second;
        MovementActionsBroadcastHeader payload{GetMapId(), recipient.GetGeneration(), tick,
            static_cast<uint16_t>((std::min)(maxRelayCount, queue.size()))};
        PacketHeader header{static_cast<uint16_t>(sizeof(PacketHeader) + sizeof(payload) + payload.count * sizeof(RelayedMovementAction)),
            static_cast<uint16_t>(GamePacketOpcode::MovementActionsBroadcast)};
        std::vector<char> buffer(header.size);
        memcpy(buffer.data(), &header, sizeof(header));
        memcpy(buffer.data() + sizeof(header), &payload, sizeof(payload));
        for (uint16_t i = 0; i < payload.count; ++i)
        {
            PendingAction item = queue.front();
            queue.pop_front();
            // 서버 대기 동안 지난 시간도 원격 클라이언트의 기준 시각에 반영한다.
            item.relay.latestClientTick += tick - item.receivedTick;
            memcpy(buffer.data() + sizeof(header) + sizeof(payload) + i * sizeof(RelayedMovementAction), &item.relay, sizeof(item.relay));
        }
        recipient.MarkMovementSent(now);
        if (recipient.GetSession() == nullptr || !recipient.GetSession()->Send(buffer.data(), static_cast<int32_t>(buffer.size())))
            failed.push_back(recipient.GetSession());
    }
    for (Session* session : failed)
    {
        if (session != nullptr)
            session->RequestClose();
    }
}

bool Map::ReceiveMovementActions(Player& player, const MovementActionsHeader& header, const MovementAction* actions)
{
    if (!IsPlatformer() || player.GetMap() != this || header.mapId != GetMapId() ||
        header.count == 0 || header.count > MAX_MOVEMENT_ACTIONS || header.latestClientTick > UINT64_MAX - 100000)
        return false;
    // 크기·범위 검사를 전체 묶음에 먼저 적용해 잘못된 후반 레코드가 부분 갱신을 만들지 않는다.
    uint64_t previousSequence = 0, previousTick = 0;
    const auto& settings = _geometry->movement;
    for (uint16_t i = 0; i < header.count; ++i)
    {
        const auto& action = actions[i];
        if (action.sequence <= previousSequence || action.clientTick < previousTick || action.clientTick > header.latestClientTick ||
            action.kind > MovementActionKind::Fall || action.horizontal < -1 || action.horizontal > 1 || action.grounded > 1 ||
            action.x < static_cast<int64_t>(_definition.minX) * 1000 || action.x > static_cast<int64_t>(_definition.maxX) * 1000 ||
            action.y < static_cast<int64_t>(_definition.minY) * 1000 || action.y > static_cast<int64_t>(_definition.maxY) * 1000 ||
            action.velocityX < -static_cast<int64_t>(settings.horizontalSpeed) * 1000 || action.velocityX > static_cast<int64_t>(settings.horizontalSpeed) * 1000 ||
            action.velocityY < -static_cast<int64_t>(settings.maxFallSpeed) * 1000 || action.velocityY > static_cast<int64_t>(settings.jumpSpeed) * 1000)
            return false;
        if (action.grounded)
        {
            const auto* support = _geometry->FindFoothold(action.footholdId);
            if (support == nullptr || action.velocityY != 0 ||
                action.x < static_cast<int64_t>(support->x1) * 1000 || action.x > static_cast<int64_t>(support->x2) * 1000 ||
                action.y != (static_cast<int64_t>(support->y1) + settings.halfHeight) * 1000)
                return false;
        }
        else if (action.footholdId != 0)
            return false;
        if (action.kind == MovementActionKind::Respawn &&
            (action.x != static_cast<int64_t>(_definition.spawnX) * 1000 || action.y != static_cast<int64_t>(_definition.spawnY) * 1000))
            return false;
        previousSequence = action.sequence;
        previousTick = action.clientTick;
    }
    if (!player.AcceptActionBatch(header.generation, header.batchSequence, header.latestClientTick))
        return false;
    for (uint16_t i = 0; i < header.count; ++i)
    {
        if (!player.AcceptAction(actions[i]))
            continue; // 착지 전 재점프와 이전 점프 번호는 중계하지 않는다.
        for (const auto& entry : _players)
        {
            auto& queue = _pendingActions[entry.first];
            if (queue.size() >= 256)
            {
                if (entry.second->GetSession() != nullptr)
                    entry.second->GetSession()->RequestClose();
                continue;
            }
            queue.push_back(PendingAction{RelayedMovementAction{player.GetCharacterId(), header.latestClientTick, actions[i]}, _tick});
        }
    }
    return true;
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
