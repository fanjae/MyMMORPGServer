#pragma once

#include "../ServerCore/Session.h"

class GameServerClient;

class LoginSession : public Session
{
public:
    LoginSession(SOCKET socket, GameServerClient& gameServerClient);

    GameServerClient& GetGameServerClient() { return _gameServerClient; }

protected:
    bool OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize) override;

private:
    GameServerClient& _gameServerClient;
};