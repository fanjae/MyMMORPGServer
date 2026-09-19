#include "GameSession.h"
#include "GamePacketHandler.h"
#include "AuthTicketManager.h"

GameSession::GameSession(SOCKET socket, AuthTicketManager& authTicketManager) : Session(socket), _authTicketManager(authTicketManager)
{
}

bool GameSession::OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize)
{
    return GamePacketHandler::Handle(*this, opcode, payload, payloadSize);
}