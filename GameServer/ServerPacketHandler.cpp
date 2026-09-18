#include "ServerPacketHandler.h"
#include "ServerPacket.h"
#include "AuthTicketManager.h"

#include <cstring>
#include <iostream>

bool ServerPacketHandler::Handle(AuthTicketManager& authTicketManager, uint16_t opcode, const char* payload, uint16_t payloadSize)
{
    switch (static_cast<ServerPacketOpcode>(opcode))
    {
    case ServerPacketOpcode::RegisterAuthTicket:
        return HandleRegisterAuthTicket(authTicketManager, payload, payloadSize);

    default:
        return false;
    }
}

bool ServerPacketHandler::HandleRegisterAuthTicket(AuthTicketManager& authTicketManager, const char* payload, uint16_t payloadSize)
{
    if (payloadSize != sizeof(RegisterAuthTicketRequest))
        return false;

    RegisterAuthTicketRequest request;
    memcpy(&request, payload, sizeof(request));

    authTicketManager.Add(request.accountId, request.authKey);

    std::cout << "Auth Ticket Registered: accountId=" << request.accountId << " authKey=" << request.authKey << "\n";
    return true;
}