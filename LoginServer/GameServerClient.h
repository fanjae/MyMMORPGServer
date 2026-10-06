#pragma once

#include <cstdint>

// 캐릭터 선택 시 LoginServer가 발급한 인증 티켓을
// 클라이언트에 전달하기 전에 GameServer에 선등록한다.
class GameServerClient
{
public:
    explicit GameServerClient(uint16_t port = 7778, uint32_t timeoutMs = 3000) : _port(port), _timeoutMs(timeoutMs) {}
    bool RegisterAuthTicket(uint32_t accountId, uint32_t characterId, uint64_t authKey);

private:
    uint16_t _port;
    uint32_t _timeoutMs;
};
