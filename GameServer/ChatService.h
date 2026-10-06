#pragma once

#include "ChatLimiter.h"
#include "../Protocol/GamePacket.h"

class Player;
class PlayerManager;

class ChatService
{
public:
    explicit ChatService(PlayerManager& players) : _players(players) {}
    bool SendMap(Player& sender, const ChatRequest& request, ChatLimiter::Clock::time_point now = ChatLimiter::Clock::now());
    bool SendWhisper(Player& sender, const WhisperRequest& request, ChatLimiter::Clock::time_point now = ChatLimiter::Clock::now());

private:
    PlayerManager& _players;
};
