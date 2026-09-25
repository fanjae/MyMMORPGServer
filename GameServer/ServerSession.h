#pragma once

#include "../ServerCore/Session.h"

class AuthTicketManager;

// LoginServer와 GameServer 사이의 내부 서버 통신 세션.
// 현재는 LoginServer가 발급한 인증 티켓을 GameServer에 등록하는 용도로 사용
// AuthTicketManager는 소유하지 않고 공유 참조
class ServerSession : public Session
{
public:
    ServerSession(SOCKET socket, AuthTicketManager& authTicketManager);

    AuthTicketManager& GetAuthTicketManager() { return _authTicketManager; }

protected:
    bool OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize) override;

private:
    AuthTicketManager& _authTicketManager;
};