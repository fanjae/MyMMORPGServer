#include "AuthTicketManager.h"

bool AuthTicketManager::Add(uint32_t accountId, uint32_t characterId, uint64_t authKey)
{
    auto expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    auto result = _tickets.emplace(authKey, AuthTicket{ accountId, characterId, authKey, expiresAt });
    return result.second;
}

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