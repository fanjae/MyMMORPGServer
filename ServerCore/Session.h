#pragma once
#include <queue>
#include <vector>
#include <winsock2.h>
#include "IocpEvent.h"
#include "RecvBuffer.h"
#include "Packet.h"

class Session
{
public:
    explicit Session(SOCKET socket);
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    SOCKET GetSocket() const { return _socket; }
    bool IsConnected() const { return _socket != INVALID_SOCKET; }
    bool CanDestroy() const { return !IsConnected() && !_recvPending && !_sendPending; }

    bool PostRecv();
    bool Send(const char* data, int32_t size);
    bool Dispatch(IocpEvent* event, DWORD bytes, bool ioSuccess);
    bool OnRecv(DWORD bytes);
    void Close();

protected:
    virtual bool OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize) = 0;

private:
    struct SendBuffer
    {
        std::vector<char> buffer;
        size_t sentBytes = 0;
    };

    bool PostSend();
    bool OnSend(DWORD bytes);
    bool ProcessPackets();

    SOCKET _socket = INVALID_SOCKET;
    RecvEvent _recvEvent;
    SendEvent _sendEvent;
    RecvBuffer _recvBuffer;
    std::queue<SendBuffer> _sendQueue;
    bool _recvPending = false;
    bool _sendPending = false;
};