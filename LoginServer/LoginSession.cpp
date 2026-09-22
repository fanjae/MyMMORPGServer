#include "AccountRepository.h"
#include "AuthKeyGenerator.h"
#include "CharacterRepository.h"
#include "GameServerClient.h"
#include "LoginSession.h"
#include "LoginPacketHandler.h"

LoginSession::LoginSession(SOCKET socket, GameServerClient& gameServerClient, AuthKeyGenerator& authKeyGenerator, AccountRepository& accountRepository, CharacterRepository& characterRepository) : Session(socket), _gameServerClient(gameServerClient), _authKeyGenerator(authKeyGenerator), _accountRepository(accountRepository), _characterRepository(characterRepository)
{
}

bool LoginSession::OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize)
{
    return LoginPacketHandler::Handle(*this, opcode, payload, payloadSize);
}