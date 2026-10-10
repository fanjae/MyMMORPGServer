#include "AuthTicketManager.h"
#include "CharacterRepository.h"
#include "GamePacketHandler.h"
#include "GameSession.h"
#include "Map.h"
#include "MapManager.h"
#include "Player.h"
#include "PlayerManager.h"
#include "ChatService.h"
#include "../Protocol/GamePacket.h"
#include "../ServerCore/Packet.h"

#include <cstring>
#include <iostream>
#include <memory>
#include <utility>

namespace
{
    constexpr uint32_t START_MAP_ID = 100000000;
}

bool GamePacketHandler::Handle(GameSession& session, uint16_t opcode, const char* payload, uint16_t payloadSize)
{
    // 경과한 중계 시각을 먼저 갱신한다. Map은 캐릭터 물리를 계산하지 않는다.
    session.GetMapManager().Advance();
    switch (static_cast<GamePacketOpcode>(opcode))
    {
    case GamePacketOpcode::EnterGameRequest:
        return HandleEnterGame(session, payload, payloadSize);

    case GamePacketOpcode::MoveRequest:
        return HandleMove(session, payload, payloadSize);

    case GamePacketOpcode::ChangeMapRequest:
        return HandleChangeMap(session, payload, payloadSize);

    case GamePacketOpcode::ChatRequest:
        return HandleChat(session, payload, payloadSize);
    case GamePacketOpcode::WhisperRequest:
        return HandleWhisper(session, payload, payloadSize);

    case GamePacketOpcode::MovementActions:
        return HandleMovementActions(session, payload, payloadSize);
    case GamePacketOpcode::MovementActions3D:
        return HandleMovementActions3D(session, payload, payloadSize);

    default:
        return false;
    }
}

namespace
{
    bool SendEnterResult(GameSession& session, const EnterGameResponse& response)
    {
        PacketHeader header;
        header.size = sizeof(PacketHeader) + sizeof(response);
        header.opcode = static_cast<uint16_t>(GamePacketOpcode::EnterGameResponse);
        char buffer[sizeof(header) + sizeof(response)];
        memcpy(buffer, &header, sizeof(header));
        memcpy(buffer + sizeof(header), &response, sizeof(response));
        return session.Send(buffer, sizeof(buffer));
    }
}

bool GamePacketHandler::HandleEnterGame(GameSession& session, const char* payload, uint16_t payloadSize)
{
    if (payloadSize != sizeof(EnterGameRequest) && payloadSize != sizeof(uint64_t))
        return false;

    EnterGameRequest request;
    request.protocolVersion = 0;
    memcpy(&request, payload, payloadSize);
    EnterGameResponse response;
    if (request.protocolVersion != GAME_PROTOCOL_VERSION && request.protocolVersion != GAME3D_PROTOCOL_VERSION)
        response.result = EnterGameResult::ProtocolMismatch;
    else if (session.IsAuthenticated())
        response.result = EnterGameResult::AlreadyAuthenticated;
    else if (session.IsAuthenticating())
        response.result = EnterGameResult::AuthenticationPending;
    else
    {
        AuthTicket ticket;
        // 인증 티켓은 한 번만 소비하고 DB 조회 중에는 같은 세션의 추가 입장을 막는다.
        if (!session.GetAuthTicketManager().Consume(request.authKey, ticket))
            response.result = EnterGameResult::InvalidAuthKey;
        else if (session.BeginCharacterLoad(ticket))
        {
            session.SetProtocolVersion(request.protocolVersion);
            return true;
        }
        else
            response.result = EnterGameResult::CharacterLoadFailed;
    }
    return SendEnterResult(session, response);
}

bool GamePacketHandler::CompleteEnterGame(GameSession& session, const AuthTicket& ticket, CharacterLoadResult loadResult)
{
    if (!session.IsConnected())
        return true;

    EnterGameResponse response;
    if (loadResult.status != CharacterLoadStatus::Success)
    {
        response.result = EnterGameResult::CharacterLoadFailed;
        std::cerr << "Character load failed: accountId=" << ticket.accountId << " characterId=" << ticket.characterId << '\n';
    }
    else
    {
        CharacterData& character = loadResult.character;
        auto player = std::make_unique<Player>(character.characterId, character.accountId, std::move(character.name), character.level);
        if (!session.GetPlayerManager().Add(*player))
            response.result = EnterGameResult::AlreadyInGame;
        else
        {
            Map* map = session.GetMapManager().FindMap(session.GetProtocolVersion() == GAME3D_PROTOCOL_VERSION ? 100000002 : START_MAP_ID);
            if (map == nullptr || !map->AddPlayer(*player))
            {
                session.GetPlayerManager().Remove(*player);
                response.result = EnterGameResult::MapEnterFailed;
            }
            else
            {
                player->SetPosition(map->GetSpawnX(), map->GetSpawnY());
                player->SetSession(&session);
                session.SetAccountId(character.accountId);
                session.SetCharacterId(character.characterId);
                session.SetPlayer(std::move(player));
                session.SetAuthenticated(true);
                const Player* enteredPlayer = session.GetPlayer();
                response.result = EnterGameResult::Success;
                response.characterId = enteredPlayer->GetCharacterId();
                if (!CopyCharacterName(response.name, enteredPlayer->GetName()))
                    return false;
                response.level = enteredPlayer->GetLevel();
                response.x = enteredPlayer->GetX();
                response.y = enteredPlayer->GetY();
                std::cout << "Game Session Authenticated: accountId=" << enteredPlayer->GetAccountId()
                    << " characterId=" << enteredPlayer->GetCharacterId() << " mapId=" << map->GetMapId() << '\n';
            }
        }
    }

    if (!SendEnterResult(session, response))
        return false;
    if (response.result == EnterGameResult::Success)
    {
        Player& player = *session.GetPlayer();
        Map& map = *player.GetMap();
        return map.SendMapInfo(player) && map.SendGeometry(player) && map.NotifyPlayerEntered(player);
    }
    return true;
}

bool GamePacketHandler::HandleMove(GameSession& session, const char* payload, uint16_t payloadSize)
{
    if (!session.IsAuthenticated() || payloadSize != sizeof(MoveRequest))
        return false;

    Player* player = session.GetPlayer();
    if (player == nullptr || player->GetMap() == nullptr)
        return false;

    MoveRequest request;
    memcpy(&request, payload, sizeof(request));

    Map* map = player->GetMap();
    MoveResponse response;
    response.sequence = request.sequence;
    response.mapId = map->GetMapId();

    if (!player->AcceptMoveSequence(request.sequence))
        response.result = MoveResult::InvalidSequence;
    else if (request.mapId != map->GetMapId())
        response.result = MoveResult::MapMismatch;
    else
        response.result = map->MovePlayer(*player, request.x, request.y);

    response.x = player->GetX();
    response.y = player->GetY();

    PacketHeader header;
    header.size = sizeof(PacketHeader) + sizeof(MoveResponse);
    header.opcode = static_cast<uint16_t>(GamePacketOpcode::MoveResponse);

    char sendBuffer[sizeof(PacketHeader) + sizeof(MoveResponse)];
    memcpy(sendBuffer, &header, sizeof(header));
    memcpy(sendBuffer + sizeof(header), &response, sizeof(response));

    // 이동 거절은 연결 오류가 아니므로 확정 좌표를 응답하고 세션을 유지한다.
    if (!session.Send(sendBuffer, sizeof(sendBuffer)))
        return false;

    return response.result != MoveResult::Success || map->NotifyPlayerMoved(*player);
}

bool GamePacketHandler::HandleChangeMap(GameSession& session, const char* payload, uint16_t payloadSize)
{
    if (!session.IsAuthenticated() || payloadSize != sizeof(ChangeMapRequest))
        return false;

    Player* player = session.GetPlayer();
    if (player == nullptr || player->GetMap() == nullptr)
        return false;

    ChangeMapRequest request;
    memcpy(&request, payload, sizeof(request));

    ChangeMapResponse response;
    Map* oldMap = player->GetMap();
    Map* newMap = session.GetMapManager().FindMap(request.mapId);
    response.mapId = oldMap->GetMapId();
    response.x = player->GetX();
    response.y = player->GetY();

    if (newMap == nullptr || newMap->Is3D() != (session.GetProtocolVersion() == GAME3D_PROTOCOL_VERSION))
    {
        response.result = ChangeMapResult::MapNotFound;
    }
    else if (newMap == oldMap)
    {
        response.result = ChangeMapResult::AlreadyInMap;
        response.mapId = oldMap->GetMapId();
        response.x = player->GetX();
        response.y = player->GetY();
    }
    else if (newMap->FindPlayer(player->GetCharacterId()) != nullptr)
    {
        response.result = ChangeMapResult::MapEnterFailed;
    }
    else
    {
        const int32_t oldX = player->GetX();
        const int32_t oldY = player->GetY();

        oldMap->NotifyPlayerLeaving(*player);
        oldMap->RemovePlayer(*player);
        player->SetPosition(newMap->GetSpawnX(), newMap->GetSpawnY());

        if (!newMap->AddPlayer(*player))
        {
            player->SetPosition(oldX, oldY);
            if (!oldMap->AddPlayer(*player))
                return false;

            response.result = ChangeMapResult::MapEnterFailed;
            response.mapId = oldMap->GetMapId();
            response.x = oldX;
            response.y = oldY;

            if (!oldMap->NotifyPlayerEntered(*player))
                return false;
        }
        else
        {
            response.result = ChangeMapResult::Success;
            response.mapId = newMap->GetMapId();
            response.x = player->GetX();
            response.y = player->GetY();
        }
    }

    PacketHeader header;
    header.size = sizeof(PacketHeader) + sizeof(ChangeMapResponse);
    header.opcode = static_cast<uint16_t>(GamePacketOpcode::ChangeMapResponse);

    char sendBuffer[sizeof(PacketHeader) + sizeof(ChangeMapResponse)];
    memcpy(sendBuffer, &header, sizeof(header));
    memcpy(sendBuffer + sizeof(header), &response, sizeof(response));

    if (!session.Send(sendBuffer, sizeof(sendBuffer)))
        return false;

    if (response.result == ChangeMapResult::Success && (!newMap->SendMapInfo(*player) || !newMap->SendGeometry(*player) || !newMap->NotifyPlayerEntered(*player)))
        return false;

    return true;
}

bool GamePacketHandler::HandleChat(GameSession& session, const char* payload, uint16_t payloadSize)
{
    if (!session.IsAuthenticated() || payloadSize != sizeof(ChatRequest))
        return false;

    Player* player = session.GetPlayer();
    if (player == nullptr || player->GetMap() == nullptr)
        return false;

    ChatRequest request;
    memcpy(&request, payload, sizeof(request));
    return ChatService(session.GetPlayerManager()).SendMap(*player, request);
}

bool GamePacketHandler::HandleWhisper(GameSession& session, const char* payload, uint16_t payloadSize)
{
    if (!session.IsAuthenticated() || payloadSize != sizeof(WhisperRequest))
        return false;
    Player* player = session.GetPlayer();
    if (player == nullptr || player->GetMap() == nullptr)
        return false;
    WhisperRequest request;
    memcpy(&request, payload, sizeof(request));
    return ChatService(session.GetPlayerManager()).SendWhisper(*player, request);
}

bool GamePacketHandler::HandleMovementActions(GameSession& session, const char* payload, uint16_t payloadSize)
{
    if (session.GetProtocolVersion() != GAME_PROTOCOL_VERSION) return false;
    if (!session.IsAuthenticated() || payloadSize < sizeof(MovementActionsHeader))
        return false;

    Player* player = session.GetPlayer();
    if (player == nullptr || player->GetMap() == nullptr)
        return false;

    MovementActionsHeader request;
    memcpy(&request, payload, sizeof(request));
    if (request.count == 0 || request.count > MAX_MOVEMENT_ACTIONS ||
        payloadSize != sizeof(request) + request.count * sizeof(MovementAction))
        return false;
    MovementAction actions[MAX_MOVEMENT_ACTIONS];
    memcpy(actions, payload + sizeof(request), request.count * sizeof(MovementAction));
    Map* map = player->GetMap();
    if (!map->IsPlatformer())
        return true;

    // 상태 검증 실패에 즉시 응답을 보내지 않아 중계 전송률 상한을 우회하지 않는다.
    map->ReceiveMovementActions(*player, request, actions);
    return true;
}

bool GamePacketHandler::HandleMovementActions3D(GameSession& session, const char* payload, uint16_t payloadSize)
{
    if (!session.IsAuthenticated() || session.GetProtocolVersion() != GAME3D_PROTOCOL_VERSION ||
        payloadSize < sizeof(MovementActionsHeader)) return false;
    Player* player = session.GetPlayer();
    if (player == nullptr || player->GetMap() == nullptr || !player->GetMap()->Is3D()) return false;
    MovementActionsHeader header;
    memcpy(&header, payload, sizeof(header));
    if (header.count == 0 || header.count > MAX_MOVEMENT_ACTIONS || payloadSize != sizeof(header) + header.count * sizeof(MovementAction3D)) return false;
    MovementAction3D actions[MAX_MOVEMENT_ACTIONS];
    memcpy(actions, payload + sizeof(header), header.count * sizeof(MovementAction3D));
    player->GetMap()->ReceiveActions3D(*player, header, actions);
    return true;
}
