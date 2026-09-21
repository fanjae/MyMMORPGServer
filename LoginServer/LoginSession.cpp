#include "AuthKeyGenerator.h"
#include "GameServerClient.h"
#include "LoginSession.h"
#include "LoginPacketHandler.h"

LoginSession::LoginSession(SOCKET socket, GameServerClient& gameServerClient, AuthKeyGenerator& authKeyGenerator) : Session(socket), _gameServerClient(gameServerClient), _authKeyGenerator(authKeyGenerator)
{
}

bool LoginSession::OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize)
{
    return LoginPacketHandler::Handle(*this, opcode, payload, payloadSize);
}