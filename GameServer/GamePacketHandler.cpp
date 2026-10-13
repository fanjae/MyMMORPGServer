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
    template<typename T> bool SendFieldPacket(GameSession& session, GamePacketOpcode opcode, const T& payload)
    {
        PacketHeader header{uint16_t(sizeof(PacketHeader)+sizeof(T)),uint16_t(opcode)};
        char bytes[sizeof(header)+sizeof(T)]; memcpy(bytes,&header,sizeof(header)); memcpy(bytes+sizeof(header),&payload,sizeof(payload));
        return session.Send(bytes,sizeof(bytes));
    }
}

bool GamePacketHandler::SendChannels(GameSession& session, ChannelResult result)
{
    auto* player=session.GetPlayer(); if (!player || !player->GetMap()) return false;
    ChannelHeader payload{player->GetMap()->GetChannel(),MapManager::ChannelCount,result};
    PacketHeader header{uint16_t(sizeof(PacketHeader)+sizeof(payload)+sizeof(ChannelRecord)*MapManager::ChannelCount),uint16_t(GamePacketOpcode::ChannelState)};
    char bytes[sizeof(header)+sizeof(payload)+sizeof(ChannelRecord)*MapManager::ChannelCount];
    memcpy(bytes,&header,sizeof(header)); memcpy(bytes+sizeof(header),&payload,sizeof(payload));
    for (uint16_t i=0;i<MapManager::ChannelCount;++i)
    {
        ChannelRecord record{uint16_t(i+1),session.GetMapManager().CountChannelPlayers(uint16_t(i+1))};
        memcpy(bytes+sizeof(header)+sizeof(payload)+i*sizeof(record),&record,sizeof(record));
    }
    return session.Send(bytes,sizeof(bytes));
}
bool GamePacketHandler::SendPickupResult(GameSession& session, uint64_t dropId, PickupResult result)
{
    return SendFieldPacket(session,GamePacketOpcode::PickupResponse3D,PickupResponse{dropId,result});
}
bool GamePacketHandler::HandleFieldRequest(GameSession& session, uint16_t opcode, const char* payload, uint16_t payloadSize)
{
    auto* player=session.GetPlayer();
    if (!session.IsAuthenticated() || session.GetProtocolVersion()!=GAME3D_PROTOCOL_VERSION || !player || !player->GetMap()) return false;
    auto operation=static_cast<GamePacketOpcode>(opcode); auto now=std::chrono::steady_clock::now();
    if (operation == GamePacketOpcode::PickupStatusRequest3D)
    {
        if (payloadSize != sizeof(uint64_t)) return false;
        InventoryMutation mutation; memcpy(&mutation.dropId, payload, sizeof(mutation.dropId));
        if (!mutation.dropId) return false;
        mutation.queryDrop = true;
        // 결과 조회는 지급을 재실행하지 않고 연결당 한 DB 작업만 허용한다.
        if (!session.QueueFieldMutation(mutation)) return SendPickupResult(session, mutation.dropId, PickupResult::Busy);
        return true;
    }
    if (operation==GamePacketOpcode::ChannelListRequest)
        return payloadSize==0 && SendChannels(session,ChannelResult::Success);
    if (operation==GamePacketOpcode::ReviveRequest)
    {
        if (payloadSize!=0) return false;
        InventoryMutation mutation; mutation.revive=true;
        if (!player->CanRevive(now) || !session.QueueFieldMutation(mutation))
            return SendInventorySnapshot(session,session.InventoryError(InventoryResult::CannotUse));
        return true;
    }
    if (operation==GamePacketOpcode::PickupRequest3D)
    {
        if (payloadSize!=sizeof(PickupRequest)) return false;
        PickupRequest request; memcpy(&request,payload,sizeof(request));
        if (!session.AcceptPickup(request.sequence) || session.FieldBusy()) return SendPickupResult(session,request.dropId,PickupResult::Busy);
        InventoryMutation mutation; auto result=player->GetMap()->ReserveDrop(*player,request,mutation);
        if (result!=PickupResult::Success) return SendPickupResult(session,request.dropId,result);
        if (!session.QueueFieldMutation(mutation,player->GetMap()))
        { player->GetMap()->FinishDrop(request.dropId,player->GetCharacterId(),InventoryResult::StorageError); return SendPickupResult(session,request.dropId,PickupResult::Busy); }
        return true;
    }
    if (payloadSize!=sizeof(uint16_t)) return false;
    uint16_t channel; memcpy(&channel,payload,sizeof(channel));
    Map* oldMap=player->GetMap(); Map* target=session.GetMapManager().FindMap(oldMap->GetMapId(),channel);
    if (!target || !target->Is3D()) return SendChannels(session,ChannelResult::InvalidChannel);
    if (target==oldMap) return SendChannels(session,ChannelResult::Success);
    if (session.FieldBusy()) return SendChannels(session,ChannelResult::Busy);
    auto result=player->BeginChannelChange(now); if (result!=ChannelResult::Success) return SendChannels(session,result);
    auto pose=player->GetState3D();
    oldMap->NotifyPlayerLeaving(*player); oldMap->RemovePlayer(*player);
    if (!target->AddPlayer(*player)) { oldMap->AddPlayer(*player); oldMap->NotifyPlayerEntered(*player); return SendChannels(session,ChannelResult::Busy); }
    // 같은 지형의 지상 위치만 유지하고 이전 채널의 행동·점프·송신 대기 목록은 재사용하지 않는다.
    pose.sequence=pose.clientTick=pose.jumpId=0; pose.velocityX=pose.velocityY=pose.velocityZ=0; pose.inputX=pose.inputZ=0;
    pose.kind=MovementActionKind::Checkpoint; player->SetState3D(pose); player->SetPosition(int32_t(pose.x/1000),int32_t(pose.y/1000));
    return SendChannels(session,ChannelResult::Success) && target->SendMapInfo(*player) && target->SendGeometry(*player) && target->NotifyPlayerEntered(*player);
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
    case GamePacketOpcode::BasicAttackRequest3D:
        return HandleAttack(session, payload, payloadSize);
    case GamePacketOpcode::InventoryRequest:
        return HandleInventory(session, payload, payloadSize);
    case GamePacketOpcode::ChannelListRequest:
    case GamePacketOpcode::ChangeChannelRequest:
    case GamePacketOpcode::ReviveRequest:
    case GamePacketOpcode::PickupRequest3D:
    case GamePacketOpcode::PickupStatusRequest3D:
        return HandleFieldRequest(session, opcode, payload, payloadSize);

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
        return map.SendMapInfo(player) && map.SendGeometry(player) && map.NotifyPlayerEntered(player) &&
            (session.GetProtocolVersion()!=GAME3D_PROTOCOL_VERSION || SendChannels(session,ChannelResult::Success));
    }
    return true;
}

bool GamePacketHandler::HandleAttack(GameSession& session, const char* payload, uint16_t payloadSize)
{
    if (!session.IsAuthenticated() || session.GetProtocolVersion() != GAME3D_PROTOCOL_VERSION ||
        session.GetPlayer() == nullptr || session.GetPlayer()->GetMap() == nullptr || payloadSize != sizeof(BasicAttackRequest)) return false;
    BasicAttackRequest request; memcpy(&request, payload, sizeof(request));
    return session.GetPlayer()->GetMap()->AttackField(*session.GetPlayer(), request, std::chrono::steady_clock::now());
}

bool GamePacketHandler::HandleInventory(GameSession& session, const char* payload, uint16_t payloadSize)
{
    // 인증된 3D 연결만 처리하며 소유 캐릭터는 요청이 아닌 세션 정보에서 가져온다.
    if (!session.IsAuthenticated() || session.GetProtocolVersion() != GAME3D_PROTOCOL_VERSION ||
        session.GetPlayer() == nullptr || payloadSize != sizeof(InventoryRequestPacket)) return false;
    InventoryRequestPacket request; memcpy(&request, payload, sizeof(request));
    if (request.operation > InventoryOperation::Unequip) return false;
    if (!session.BeginInventoryRequest(request)) { auto error=session.InventoryError(InventoryResult::RateLimited); error.requestId=request.requestId; return SendInventorySnapshot(session,error); }
    return true;
}

bool GamePacketHandler::SendInventorySnapshot(GameSession& session, const InventorySnapshotPacket& snapshot)
{
    // 성공·거절 모두 전체 상태를 보내 클라이언트가 서버 기준으로 화면을 갱신한다.
    PacketHeader header{static_cast<uint16_t>(sizeof(PacketHeader) + sizeof(snapshot)), static_cast<uint16_t>(GamePacketOpcode::InventorySnapshot)};
    char bytes[sizeof(header) + sizeof(snapshot)];
    memcpy(bytes, &header, sizeof(header)); memcpy(bytes + sizeof(header), &snapshot, sizeof(snapshot));
    return session.Send(bytes, sizeof(bytes));
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
    Map* newMap = session.GetMapManager().FindMap(request.mapId, oldMap->GetChannel());
    response.mapId = oldMap->GetMapId();
    response.x = player->GetX();
    response.y = player->GetY();

    if (newMap == nullptr || newMap->Is3D() != (session.GetProtocolVersion() == GAME3D_PROTOCOL_VERSION))
    {
        response.result = ChangeMapResult::MapNotFound;
    }
    else if (player->IsDead() || player->CombatPaused())
    {
        response.result=ChangeMapResult::MapEnterFailed;
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
