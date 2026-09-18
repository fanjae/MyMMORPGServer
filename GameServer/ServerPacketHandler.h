#pragma once

#include <cstdint>

class AuthTicketManager;

class ServerPacketHandler
{
public:
    static bool Handle(AuthTicketManager& authTicketManager, uint16_t opcode, const char* payload, uint16_t payloadSize);

private:
    static bool HandleRegisterAuthTicket(AuthTicketManager& authTicketManager, const char* payload, uint16_t payloadSize);
};