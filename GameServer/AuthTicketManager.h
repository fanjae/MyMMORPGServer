#pragma once

#include <cstdint>
#include <unordered_map>

struct AuthTicket
{
    uint32_t accountId = 0;
    uint64_t authKey = 0;
};

class AuthTicketManager
{
public:
    void Add(uint32_t accountId, uint64_t authKey);
    bool Consume(uint64_t authKey, AuthTicket& ticket);

private:
    std::unordered_map<uint64_t, AuthTicket> _tickets;
};