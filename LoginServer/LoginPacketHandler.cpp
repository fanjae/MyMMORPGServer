#include "LoginPacketHandler.h"
#include "LoginPacket.h"
#include "LoginSession.h"
#include "../ServerCore/Packet.h"

#include <cstring>
#include <iostream>

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

    // 임시 로그인 검증
    if (request.accountId == 1)
    {
        response.result = LoginResult::Success;
        response.authKey = 123456789;
        response.gameServerPort = 7777;

        std::cout << "Login Success\n";
    }
    else
    {
        response.result = LoginResult::InvalidAccount;
    }

    PacketHeader header;
    header.size = sizeof(PacketHeader) + sizeof(LoginResponse);
    header.opcode = static_cast<uint16_t>(LoginPacketOpcode::LoginResponse);

    char sendBuffer[sizeof(PacketHeader) + sizeof(LoginResponse)];

    memcpy(sendBuffer, &header, sizeof(header));
    memcpy(sendBuffer + sizeof(header), &response, sizeof(response));

    return session.Send(sendBuffer, sizeof(sendBuffer));
}