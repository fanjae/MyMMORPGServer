#include "../Protocol/ServerPacket.h"
#include "../ServerCore/Packet.h"
#include "ServerPacketHandler.h"
#include "ServerSession.h"
#include "AuthTicketManager.h"

#include <cstring>
#include <iostream>

bool ServerPacketHandler::Handle(ServerSession& session, uint16_t opcode, const char* payload, uint16_t payloadSize)
{
    switch (static_cast<ServerPacketOpcode>(opcode))
    {
    case ServerPacketOpcode::RegisterAuthTicketRequest:
        return HandleRegisterAuthTicket(session, payload, payloadSize);

    default:
        return false;
    }
}

bool ServerPacketHandler::HandleRegisterAuthTicket(ServerSession& session, const char* payload, uint16_t payloadSize)
{
    if (payloadSize != sizeof(RegisterAuthTicketRequest))
        return false;

    RegisterAuthTicketRequest request;
    memcpy(&request, payload, sizeof(request));

    session.GetAuthTicketManager().Add(request.accountId, request.characterId, request.authKey);

    std::cout << "Auth Ticket Registered: accountId=" << request.accountId << " characterId=" << request.characterId << " authKey=" << request.authKey << "\n";

    RegisterAuthTicketResponse response;
    response.result = RegisterAuthTicketResult::Success;
    response.authKey = request.authKey;

    PacketHeader header;
    header.size = sizeof(PacketHeader) + sizeof(RegisterAuthTicketResponse);
    header.opcode = static_cast<uint16_t>(ServerPacketOpcode::RegisterAuthTicketResponse);

    char sendBuffer[sizeof(PacketHeader) + sizeof(RegisterAuthTicketResponse)];

    memcpy(sendBuffer, &header, sizeof(header));
    memcpy(sendBuffer + sizeof(header), &response, sizeof(response));

    // 인증 티켓 등록 결과를 로그인 서버에 전달
    return session.Send(sendBuffer, sizeof(sendBuffer));
}