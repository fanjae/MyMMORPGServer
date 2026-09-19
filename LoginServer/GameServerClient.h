#pragma once

#include <cstdint>

class GameServerClient
{
public:
    bool RegisterAuthTicket(uint32_t accountId, uint64_t authKey);
};