#include "LoginSession.h"
#include "LoginPacketHandler.h"

LoginSession::LoginSession(SOCKET socket) : Session(socket)
{
}

bool LoginSession::OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize)
{
    return LoginPacketHandler::Handle(*this, opcode, payload, payloadSize);
}