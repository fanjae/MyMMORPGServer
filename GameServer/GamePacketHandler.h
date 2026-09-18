#pragma once

#include <cstdint>

class GameSession;

class GamePacketHandler
{
public:
    static bool Handle(GameSession& session, uint16_t opcode, const char* payload, uint16_t payloadSize);

private:
    static bool HandleEnterGame(GameSession& session, const char* payload, uint16_t payloadSize);
};