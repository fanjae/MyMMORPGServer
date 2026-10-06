#pragma once

#include "../ServerCore/Session.h"

#include <memory>
#include "CharacterRepository.h"
#include "AuthTicketManager.h"

class AuthTicketManager;
class CharacterRepository;
class MapManager;
class Player;
class PlayerManager;

// 게임 클라이언트와의 연결을 나타내며 인증 완료 후 Player를 소유
// AuthTicketManager와 CharacterRepository는 소유하지 않고 참조하므로 GameSession보다 오래 유지 필요
class GameSession : public Session
{
public:
    GameSession(SOCKET socket, AuthTicketManager& authTicketManager, CharacterRepository& characterRepository, PlayerManager& playerManager, MapManager& mapManager);
    ~GameSession() override;

    bool IsAuthenticated() const { return _authenticated; }
    void SetAuthenticated(bool authenticated) { _authenticated = authenticated; }

    uint32_t GetAccountId() const { return _accountId; }
    void SetAccountId(uint32_t accountId) { _accountId = accountId; }

    uint32_t GetCharacterId() const { return _characterId; }
    void SetCharacterId(uint32_t characterId) { _characterId = characterId; }

    AuthTicketManager& GetAuthTicketManager() { return _authTicketManager; }
    CharacterRepository& GetCharacterRepository() { return _characterRepository; }
    PlayerManager& GetPlayerManager() { return _playerManager; }
    MapManager& GetMapManager() { return _mapManager; }

    Player* GetPlayer() const { return _player.get(); }
    void SetPlayer(std::unique_ptr<Player> player);
    bool BeginCharacterLoad(const AuthTicket& ticket);
    bool IsAuthenticating() const { return _characterLoad.valid(); }
    void Update() override;

protected:
    bool OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize) override;
    void OnDisconnected() override;

private:
    AuthTicketManager& _authTicketManager;
    CharacterRepository& _characterRepository;
    PlayerManager& _playerManager;
    MapManager& _mapManager;
    std::unique_ptr<Player> _player;

    uint32_t _accountId = 0;
    uint32_t _characterId = 0;
    bool _authenticated = false;
    AuthTicket _pendingTicket;
    std::future<CharacterLoadResult> _characterLoad;
    std::shared_ptr<std::atomic<bool>> _loadCanceled;
    std::chrono::steady_clock::time_point _loadDeadline;
};
