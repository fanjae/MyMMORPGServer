#include "GameSession.h"
#include "GamePacketHandler.h"

GameSession::GameSession(SOCKET socket) : Session(socket)
{
}

bool GameSession::OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize)
{
    return GamePacketHandler::Handle(*this, opcode, payload, payloadSize);
}