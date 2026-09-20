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
}

bool LoginPacketHandler::Handle(LoginSession& session, uint16_t opcode, const char* payload, uint16_t payloadSize)
{
    switch (static_cast<LoginPacketOpcode>(opcode))
    {
    case LoginPacketOpcode::LoginRequest:
        return HandleLogin(session, payload, payloadSize);

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
        uint64_t authKey = nextAuthKey++;

        if (!session.GetGameServerClient().RegisterAuthTicket(request.accountId, authKey))
        {
            response.result = LoginResult::ServerUnavailable;
        }
        else
        {
            response.result = LoginResult::Success;
            response.authKey = authKey;
            response.gameServerPort = 7777;
        }
    }

    PacketHeader header;
    header.size = sizeof(PacketHeader) + sizeof(LoginResponse);
    header.opcode = static_cast<uint16_t>(LoginPacketOpcode::LoginResponse);

    char sendBuffer[sizeof(PacketHeader) + sizeof(LoginResponse)];

    memcpy(sendBuffer, &header, sizeof(header));
    memcpy(sendBuffer + sizeof(header), &response, sizeof(response));

    return session.Send(sendBuffer, sizeof(sendBuffer));
}