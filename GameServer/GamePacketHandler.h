#pragma once

#include <cstdint>

class GameSession;
struct AuthTicket;
struct CharacterLoadResult;

class GamePacketHandler
{
public:
    static bool Handle(GameSession& session, uint16_t opcode, const char* payload, uint16_t payloadSize);
    static bool CompleteEnterGame(GameSession& session, const AuthTicket& ticket, CharacterLoadResult result);

private:
    static bool HandleEnterGame(GameSession& session, const char* payload, uint16_t payloadSize);
    static bool HandleMove(GameSession& session, const char* payload, uint16_t payloadSize);
    static bool HandleChangeMap(GameSession& session, const char* payload, uint16_t payloadSize);
    static bool HandleChat(GameSession& session, const char* payload, uint16_t payloadSize);
    static bool HandleWhisper(GameSession& session, const char* payload, uint16_t payloadSize);
    static bool HandleMovementInput(GameSession& session, const char* payload, uint16_t payloadSize);
};
