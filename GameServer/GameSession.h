#pragma once

#include "../ServerCore/Session.h"

class AuthTicketManager;

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