#pragma once
#include <queue>
#include <vector>
#include <winsock2.h>
#include "IocpEvent.h"
#include "RecvBuffer.h"
#include "Packet.h"

// 하나의 TCP 연결과 그 연결의 비동기 Recv/Send 상태를 소유
// Recv/Send Event는 Session 멤버이므로 pending I/O가 모두 완료될 때까지
// Session 객체의 주소와 수명이 유지되어야 한다.
class Session
{
public:
    explicit Session(SOCKET socket);
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    SOCKET GetSocket() const { return _socket; }
    bool IsConnected() const { return _socket != INVALID_SOCKET; }

    // closesocket 이후에도 취소된 overlapped I/O의 completion이 도착할 수 있으므로
    // socket이 닫히고 모든 pending I/O가 회수된 뒤에만 Session을 파괴한다.
    bool CanDestroy() const { return !IsConnected() && !_recvPending && !_sendPending; }

    bool PostRecv();
    bool Send(const char* data, int32_t size);
    bool Dispatch(IocpEvent* event, DWORD bytes, bool ioSuccess);
    bool OnRecv(DWORD bytes);
    void Close();

protected:
    virtual bool OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize) = 0;

    // 논리적인 연결 종료 시 파생 Session이 게임 로직 정리를 수행하기 위한 hook.
    // 실제 Session 객체 수명은 pending I/O completion이 모두 회수될 때까지 유지
    virtual void OnDisconnected() {}

private:

    // 하나의 논리적 Send 요청에서 아직 완료되지 않은 전송 상태.
    // WSASend가 일부 바이트만 완료할 수 있으므로 sentBytes를 누적한다.
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
    bool _disconnectHandled = false; 
};