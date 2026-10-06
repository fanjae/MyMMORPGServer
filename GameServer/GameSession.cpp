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

bool GameSession::BeginCharacterLoad(const AuthTicket& ticket)
{
    _loadCanceled = std::make_shared<std::atomic<bool>>(false);
    _characterLoad = _characterRepository.FindByIdAsync(ticket.accountId, ticket.characterId, _loadCanceled);
    if (!_characterLoad.valid())
        return false;
    _pendingTicket = ticket;
    _loadDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    return true;
}

void GameSession::Update()
{
    if (!_characterLoad.valid())
        return;

    if (std::chrono::steady_clock::now() >= _loadDeadline)
    {
        RequestClose();
        return;
    }

    if (_characterLoad.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;

    CharacterLoadResult result;
    try
    {
        result = _characterLoad.get();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Character load worker failed: " << exception.what() << '\n';
    }

    // DB 완료 후의 Player 생성과 Map 등록은 IOCP 실행 스레드에서만 처리한다.
    if (!GamePacketHandler::CompleteEnterGame(*this, _pendingTicket, std::move(result)))
        RequestClose();
}

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
    // 이전 연결의 DB 결과는 버리고 새 연결의 인증과 Player 상태에 적용하지 않는다.
    if (_loadCanceled != nullptr)
        _loadCanceled->store(true);
    _characterLoad = {};
    _pendingTicket = {};
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
