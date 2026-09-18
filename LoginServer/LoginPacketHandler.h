#pragma once

#include <cstdint>

class LoginSession;

class LoginPacketHandler
{
public:
    static bool Handle(LoginSession& session, uint16_t opcode, const char* payload, uint16_t payloadSize);

private:
    static bool HandleLogin(LoginSession& session, const char* payload, uint16_t payloadSize);
};