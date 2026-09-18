#include "AuthTicketManager.h"

void AuthTicketManager::Add(uint32_t accountId, uint64_t authKey)
{
    _tickets[authKey] = AuthTicket{ accountId, authKey };
}

bool AuthTicketManager::Consume(uint64_t authKey, AuthTicket& ticket)
{
    auto it = _tickets.find(authKey);
    if (it == _tickets.end())
        return false;

    ticket = it->second;
    _tickets.erase(it);
    return true;
}