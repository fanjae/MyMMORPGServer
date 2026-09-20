#pragma once

#include "../ServerCore/Session.h"

class AuthTicketManager;

class ServerSession : public Session
{
public:
    ServerSession(SOCKET socket, AuthTicketManager& authTicketManager);

    AuthTicketManager& GetAuthTicketManager() { return _authTicketManager; }

protected:
    bool OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize) override;

private:
    AuthTicketManager& _authTicketManager;
};