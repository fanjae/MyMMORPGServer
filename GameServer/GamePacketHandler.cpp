#include "AuthTicketManager.h"
#include "CharacterRepository.h"
#include "GamePacketHandler.h"
#include "GameSession.h"
#include "Map.h"
#include "MapManager.h"
#include "Player.h"
#include "PlayerManager.h"
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

    default:
        return false;
    }
}

bool GamePacketHandler::HandleEnterGame(GameSession& session, const char* payload, uint16_t payloadSize)
{
    if (payloadSize != sizeof(EnterGameRequest))
        return false;

    EnterGameRequest request;
    memcpy(&request, payload, sizeof(request));

    EnterGameResponse response;

    if (session.IsAuthenticated())
    {
        response.result = EnterGameResult::AlreadyAuthenticated;
    }
    else
    {
        AuthTicket ticket;

        // LoginServer가 등록한 일회용 인증 티켓을 소비한다.
        // accountId와 characterId는 클라이언트가 아니라 인증된 티켓을 기준으로 사용한다.
        if (!session.GetAuthTicketManager().Consume(request.authKey, ticket))
        {
            response.result = EnterGameResult::InvalidAuthKey;
        }
        else
        {
            CharacterLoadResult loadResult = session.GetCharacterRepository().FindById(ticket.accountId, ticket.characterId);

            if (loadResult.status != CharacterLoadStatus::Success)
            {
                response.result = EnterGameResult::CharacterLoadFailed;

                if (loadResult.status == CharacterLoadStatus::NotFound)
                {
                    std::cerr << "Character not found: accountId=" << ticket.accountId << " characterId=" << ticket.characterId << '\n';
                }
                else
                {
                    std::cerr << "Character load failed: accountId=" << ticket.accountId << " characterId=" << ticket.characterId << '\n';
                }
            }
            else
            {
                CharacterData& character = loadResult.character;
                auto player = std::make_unique<Player>(character.characterId, character.accountId, std::move(character.name), character.level);

                if (!session.GetPlayerManager().Add(*player))
                {
                    response.result = EnterGameResult::AlreadyInGame;

                    std::cerr << "Player already in game: accountId=" << player->GetAccountId()
                        << " characterId=" << player->GetCharacterId() << '\n';
                }
                else
                {
                    Map* map = session.GetMapManager().FindMap(START_MAP_ID);

                    if (map == nullptr || !map->AddPlayer(*player))
                    {
                        session.GetPlayerManager().Remove(*player);
                        response.result = EnterGameResult::MapEnterFailed;

                        std::cerr << "Player map enter failed: accountId=" << player->GetAccountId()
                            << " characterId=" << player->GetCharacterId()
                            << " mapId=" << START_MAP_ID << '\n';
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

                        if (enteredPlayer == nullptr)
                            return false;

                        response.result = EnterGameResult::Success;
                        response.characterId = enteredPlayer->GetCharacterId();
                        strcpy_s(response.name, enteredPlayer->GetName().c_str());
                        response.level = enteredPlayer->GetLevel();
                        response.x = enteredPlayer->GetX();
                        response.y = enteredPlayer->GetY();

                        std::cout << "Game Session Authenticated: accountId=" << enteredPlayer->GetAccountId()
                            << " characterId=" << enteredPlayer->GetCharacterId()
                            << " name=" << enteredPlayer->GetName()
                            << " level=" << enteredPlayer->GetLevel()
                            << " mapId=" << enteredPlayer->GetMap()->GetMapId() << '\n';
                    }
                }
            }
        }
    }

    PacketHeader header;
    header.size = sizeof(PacketHeader) + sizeof(EnterGameResponse);
    header.opcode = static_cast<uint16_t>(GamePacketOpcode::EnterGameResponse);

    char sendBuffer[sizeof(PacketHeader) + sizeof(EnterGameResponse)];

    memcpy(sendBuffer, &header, sizeof(header));
    memcpy(sendBuffer + sizeof(header), &response, sizeof(response));

    if (!session.Send(sendBuffer, sizeof(sendBuffer)))
        return false;

    if (response.result == EnterGameResult::Success)
    {
        Player* enteredPlayer = session.GetPlayer();
        if (enteredPlayer == nullptr || enteredPlayer->GetMap() == nullptr)
            return false;

        if (!enteredPlayer->GetMap()->NotifyPlayerEntered(*enteredPlayer))
            return false;
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

    return player->GetMap()->MovePlayer(*player, request.x, request.y);
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

    if (newMap == nullptr)
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

    if (response.result == ChangeMapResult::Success && !newMap->NotifyPlayerEntered(*player))
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
    request.message[MAX_CHAT_MESSAGE_LENGTH - 1] = '\0';

    if (request.message[0] == '\0')
        return true;

    return player->GetMap()->NotifyPlayerChat(*player, request.message);
}
