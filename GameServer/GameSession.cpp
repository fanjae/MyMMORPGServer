#include "GameSession.h"
#include "AuthTicketManager.h"
#include "CharacterRepository.h"
#include "GamePacketHandler.h"
#include "Map.h"
#include "Player.h"
#include "PlayerManager.h"

#include <iostream>
#include <utility>

GameSession::GameSession(SOCKET socket, AuthTicketManager& authTicketManager, CharacterRepository& characterRepository, PlayerManager& playerManager, MapManager& mapManager) : Session(socket), _authTicketManager(authTicketManager), _characterRepository(characterRepository), _playerManager(playerManager), _mapManager(mapManager)
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
    if (_player != nullptr)
    {
        if (Map* map = _player->GetMap())
        {
            map->NotifyPlayerLeaving(*_player);
            map->RemovePlayer(*_player);
        }

        _playerManager.Remove(*_player);
        _player->SetSession(nullptr);

        std::cout << "Player Left Game: accountId=" << _player->GetAccountId()
            << " characterId=" << _player->GetCharacterId()
            << " name=" << _player->GetName() << '\n';
    }

    _authenticated = false;
    _accountId = 0;
    _characterId = 0;
    _player.reset();
}