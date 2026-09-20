#pragma once

#include <cstdint>

class ServerSession;

class ServerPacketHandler
{
public:
    static bool Handle(ServerSession& session, uint16_t opcode, const char* payload, uint16_t payloadSize);

private:
    static bool HandleRegisterAuthTicket(ServerSession& session, const char* payload, uint16_t payloadSize);
};