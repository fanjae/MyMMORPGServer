#pragma once

#include "../ServerCore/Session.h"

class AuthTicketManager;

// 게임 클라이언트와의 연결을 나타내며 인증 완료 후 account/character 식별자를 보관
// AuthTicketManager는 소유하지 않고 참조하므로 GameSession보다 오래 유지
class GameSession : public Session
{
public:
    GameSession(SOCKET socket, AuthTicketManager& authTicketManager);

    bool IsAuthenticated() const { return _authenticated; }
    void SetAuthenticated(bool authenticated) { _authenticated = authenticated; }

    uint32_t GetAccountId() const { return _accountId; }
    void SetAccountId(uint32_t accountId) { _accountId = accountId; }

    uint32_t GetCharacterId() const { return _characterId; }
    void SetCharacterId(uint32_t characterId) { _characterId = characterId; }

    AuthTicketManager& GetAuthTicketManager() { return _authTicketManager; }

protected:
    bool OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize) override;
    void OnDisconnected() override;

private:
    AuthTicketManager& _authTicketManager;
    uint32_t _accountId = 0;
    uint32_t _characterId = 0;
    bool _authenticated = false;
};