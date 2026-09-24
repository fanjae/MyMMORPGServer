#include "GameSession.h"
#include "GamePacketHandler.h"
#include "AuthTicketManager.h"
#include <iostream>

GameSession::GameSession(SOCKET socket, AuthTicketManager& authTicketManager) : Session(socket), _authTicketManager(authTicketManager)
{
}

bool GameSession::OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize)
{
    return GamePacketHandler::Handle(*this, opcode, payload, payloadSize);
}

void GameSession::OnDisconnected()
{
    std::cout << "GameSession Disconnected: characterId=" << _characterId << '\n';
}