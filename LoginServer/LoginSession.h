#pragma once

#include "../ServerCore/Session.h"

class LoginSession : public Session
{
public:
    explicit LoginSession(SOCKET socket);

protected:
    bool OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize) override;
};