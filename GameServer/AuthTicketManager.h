#pragma once

#include <cstdint>
#include <chrono>
#include <unordered_map>

struct AuthTicket
{
    uint32_t accountId = 0;
    uint32_t characterId = 0;
    uint64_t authKey = 0;
    std::chrono::steady_clock::time_point expiresAt;
};

// LoginServer가 발급한 게임 입장용 인증 티켓을 임시 보관한다.
// 티켓은 제한된 유효 시간을 가지며, 인증에 성공하면 한 번만 소비된다.
class AuthTicketManager
{
public:
    bool Add(uint32_t accountId, uint32_t characterId, uint64_t authKey);
    bool Consume(uint64_t authKey, AuthTicket& ticket);
    void CleanupExpired();

private:
    std::unordered_map<uint64_t, AuthTicket> _tickets;
};