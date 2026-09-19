#include "GameServerClient.h"
#include "LoginSession.h"
#include "LoginPacketHandler.h"

LoginSession::LoginSession(SOCKET socket, GameServerClient& gameServerClient) : Session(socket), _gameServerClient(gameServerClient)
{
}

bool LoginSession::OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize)
{
    return LoginPacketHandler::Handle(*this, opcode, payload, payloadSize);
}