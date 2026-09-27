#include "GameSession.h"
#include "AuthTicketManager.h"
#include "CharacterRepository.h"
#include "GamePacketHandler.h"
#include "Player.h"

#include <iostream>
#include <utility>

GameSession::GameSession(SOCKET socket, AuthTicketManager& authTicketManager, CharacterRepository& characterRepository) : Session(socket), _authTicketManager(authTicketManager), _characterRepository(characterRepository)
{
}

GameSession::~GameSession() = default;

void GameSession::SetPlayer(std::unique_ptr<Player> player)
{
    _player = std::move(player);
}

bool GameSession::OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize)
{
    return GamePacketHandler::Handle(*this, opcode, payload, payloadSize);
}

void GameSession::OnDisconnected()
{
    std::cout << "GameSession Disconnected: characterId=" << _characterId << '\n';
}