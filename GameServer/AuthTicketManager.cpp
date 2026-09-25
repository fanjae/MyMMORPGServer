#include "AuthTicketManager.h"

// 티켓은 발급 후 30초 동안만 게임 입장 인증에 사용할 수 있다.
// 시스템 시간 변경의 영향을 받지 않도록 만료 계산에는 steady_clock을 사용한다.
bool AuthTicketManager::Add(uint32_t accountId, uint32_t characterId, uint64_t authKey)
{
    auto expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    auto result = _tickets.emplace(authKey, AuthTicket{ accountId, characterId, authKey, expiresAt });
    return result.second;
}

// 유효한 티켓은 조회와 동시에 제거해 동일 authKey의 재사용을 막는다.
bool AuthTicketManager::Consume(uint64_t authKey, AuthTicket& ticket)
{
    auto it = _tickets.find(authKey);
    if (it == _tickets.end())
        return false;

    if (std::chrono::steady_clock::now() >= it->second.expiresAt)
    {
        _tickets.erase(it);
        return false;
    }

    ticket = it->second;
    _tickets.erase(it);
    return true;
}

void AuthTicketManager::CleanupExpired()
{
    auto now = std::chrono::steady_clock::now();

    for (auto it = _tickets.begin(); it != _tickets.end();)
    {
        if (now >= it->second.expiresAt)
            it = _tickets.erase(it);
        else
            ++it;
    }
}