#pragma once

#include <cstdint>

class GameServerClient
{
public:
    bool RegisterAuthTicket(uint32_t accountId, uint32_t characterId, uint64_t authKey);
};