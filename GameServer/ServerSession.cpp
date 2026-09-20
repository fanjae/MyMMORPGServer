#include "ServerSession.h"
#include "ServerPacketHandler.h"

ServerSession::ServerSession(SOCKET socket, AuthTicketManager& authTicketManager) : Session(socket), _authTicketManager(authTicketManager)
{
}

bool ServerSession::OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize)
{
    return ServerPacketHandler::Handle(*this, opcode, payload, payloadSize);
}