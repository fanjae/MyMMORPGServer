#include "GameServerClient.h"
#include "LoginPacketHandler.h"
#include "LoginPacket.h"
#include "LoginSession.h"
#include "../ServerCore/Packet.h"

#include <cstring>
#include <iostream>

namespace
{
    uint64_t nextAuthKey = 1;

    bool IsValidCharacter(uint32_t accountId, uint32_t characterId)
    {
        if (accountId != 1)
            return false;

        return characterId == 1001 || characterId == 1002;
    }
}

bool LoginPacketHandler::Handle(LoginSession& session, uint16_t opcode, const char* payload, uint16_t payloadSize)
{
    switch (static_cast<LoginPacketOpcode>(opcode))
    {
    case LoginPacketOpcode::LoginRequest:
        return HandleLogin(session, payload, payloadSize);

    case LoginPacketOpcode::CharacterListRequest:
        return HandleCharacterList(session, payload, payloadSize);

    case LoginPacketOpcode::CharacterSelectRequest:
        return HandleCharacterSelect(session, payload, payloadSize);

    default:
        return false;
    }
}

bool LoginPacketHandler::HandleLogin(LoginSession& session, const char* payload, uint16_t payloadSize)
{
    if (payloadSize != sizeof(LoginRequest))
        return false;

    LoginRequest request;
    memcpy(&request, payload, sizeof(request));

    LoginResponse response;

    if (request.accountId != 1)
    {
        response.result = LoginResult::InvalidAccount;
    }
    else
    {
        session.SetAuthenticated(true);
        session.SetAccountId(request.accountId);
        response.result = LoginResult::Success;
    }

    PacketHeader header;
    header.size = sizeof(PacketHeader) + sizeof(LoginResponse);
    header.opcode = static_cast<uint16_t>(LoginPacketOpcode::LoginResponse);

    char sendBuffer[sizeof(PacketHeader) + sizeof(LoginResponse)];

    memcpy(sendBuffer, &header, sizeof(header));
    memcpy(sendBuffer + sizeof(header), &response, sizeof(response));

    return session.Send(sendBuffer, sizeof(sendBuffer));
}

bool LoginPacketHandler::HandleCharacterList(LoginSession& session, const char* payload, uint16_t payloadSize)
{
    if (payloadSize != 0)
        return false;

    if (!session.IsAuthenticated())
        return false;

    CharacterListResponse response;
    response.characterCount = 2;

    response.characters[0].characterId = 1001;
    memcpy(response.characters[0].name, "Warrior", sizeof("Warrior"));
    strcpy_s(response.characters[0].name, "Warrior");
    response.characters[0].level = 10;

    response.characters[1].characterId = 1002;
    memcpy(response.characters[1].name, "Magician", sizeof("Magician"));
    strcpy_s(response.characters[1].name, "Magician");
    response.characters[1].level = 15;

    PacketHeader header;
    header.size = sizeof(PacketHeader) + sizeof(CharacterListResponse);
    header.opcode = static_cast<uint16_t>(LoginPacketOpcode::CharacterListResponse);

    char sendBuffer[sizeof(PacketHeader) + sizeof(CharacterListResponse)];

    memcpy(sendBuffer, &header, sizeof(header));
    memcpy(sendBuffer + sizeof(header), &response, sizeof(response));

    return session.Send(sendBuffer, sizeof(sendBuffer));
}

bool LoginPacketHandler::HandleCharacterSelect(LoginSession& session, const char* payload, uint16_t payloadSize)
{
    if (payloadSize != sizeof(CharacterSelectRequest))
        return false;

    if (!session.IsAuthenticated())
        return false;

    CharacterSelectRequest request;
    memcpy(&request, payload, sizeof(request));

    CharacterSelectResponse response;

    if (!IsValidCharacter(session.GetAccountId(), request.characterId))
    {
        response.result = CharacterSelectResult::InvalidCharacter;
    }
    else
    {
        uint64_t authKey = nextAuthKey++;

        if (!session.GetGameServerClient().RegisterAuthTicket(session.GetAccountId(), request.characterId, authKey))
        {
            response.result = CharacterSelectResult::ServerUnavailable;
        }
        else
        {
            response.result = CharacterSelectResult::Success;
            response.authKey = authKey;
            response.gameServerPort = 7777;
        }
    }

    PacketHeader header;
    header.size = sizeof(PacketHeader) + sizeof(CharacterSelectResponse);
    header.opcode = static_cast<uint16_t>(LoginPacketOpcode::CharacterSelectResponse);

    char sendBuffer[sizeof(PacketHeader) + sizeof(CharacterSelectResponse)];

    memcpy(sendBuffer, &header, sizeof(header));
    memcpy(sendBuffer + sizeof(header), &response, sizeof(response));

    return session.Send(sendBuffer, sizeof(sendBuffer));
}