#pragma once

#include "../ServerCore/Session.h"

class GameSession : public Session
{
public:
    explicit GameSession(SOCKET socket);

    bool IsAuthenticated() const { return _authenticated; }
    void SetAuthenticated(bool authenticated) { _authenticated = authenticated; }

protected:
    bool OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize) override;

private:
    bool _authenticated = false;
};