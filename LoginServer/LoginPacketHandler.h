#pragma once

#include <cstdint>
#include "../Protocol/LoginPacket.h"

class LoginSession;

class LoginPacketHandler
{
public:
    static bool Handle(LoginSession& session, uint16_t opcode, const char* payload, uint16_t payloadSize);
    static bool SendRegistration(LoginSession& session, RegistrationResult result);

private:
    static bool HandleRegistration(LoginSession& session, const char* payload, uint16_t payloadSize);
    static bool HandleLogin(LoginSession& session, const char* payload, uint16_t payloadSize);
    static bool HandleCharacterList(LoginSession& session, const char* payload, uint16_t payloadSize);
    static bool HandleCharacterSelect(LoginSession& session, const char* payload, uint16_t payloadSize);
};
