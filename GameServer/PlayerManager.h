#pragma once

#include <cstdint>
#include <unordered_map>
#include "ChatLimiter.h"

class Player;

class PlayerManager
{
public:
    bool Add(Player& player);
    void Remove(Player& player);

    Player* FindByCharacterId(uint32_t characterId) const;
    Player* FindByAccountId(uint32_t accountId) const;
    ChatLimiter& GetChatLimiter() { return _chatLimiter; }

private:
    std::unordered_map<uint32_t, Player*> _playersByCharacterId;
    std::unordered_map<uint32_t, Player*> _playersByAccountId;
    // Map 변경과 Player 재생성으로 채팅 제한을 초기화하지 않도록 계정별로 보관한다.
    ChatLimiter _chatLimiter;
};
