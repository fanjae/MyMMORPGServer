#pragma once

#include "../ServerCore/Session.h"

class AuthKeyGenerator;
class GameServerClient;

class LoginSession : public Session
{
public:
    LoginSession(SOCKET socket, GameServerClient& gameServerClient, AuthKeyGenerator& authKeyGenerator);

    bool IsAuthenticated() const { return _authenticated; }
    void SetAuthenticated(bool authenticated) { _authenticated = authenticated; }

    uint32_t GetAccountId() const { return _accountId; }
    void SetAccountId(uint32_t accountId) { _accountId = accountId; }

    GameServerClient& GetGameServerClient() { return _gameServerClient; }
    AuthKeyGenerator& GetAuthKeyGenerator() { return _authKeyGenerator; }

protected:
    bool OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize) override;

private:
    GameServerClient& _gameServerClient;
    AuthKeyGenerator& _authKeyGenerator;
    uint32_t _accountId = 0;
    bool _authenticated = false;
};