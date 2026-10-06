#include "pch.h"
#include "Session.h"
#include "SocketUtils.h"

#include <iostream>
#include <algorithm>
#include <utility>

Session::Session(SOCKET socket) : _socket(socket), _recvBuffer(MAX_PACKET_SIZE)
{
}

Session::~Session()
{
    SocketUtils::Close(_socket);
}

void Session::Close()
{
    _closeRequested = true;
    // socket 종료보다 먼저 논리적인 disconnect 처리를 수행한다.
    // 실제 객체 파괴는 pending I/O completion 회수 후 SessionManager가 담당한다.
    if (!_disconnectHandled)
    {
        _disconnectHandled = true;
        OnDisconnected();
    }

    SocketUtils::Close(_socket);
}

bool Session::PostRecv()
{
    if (!IsConnected())
        return false;

    _recvEvent.overlapped = {};
    // 미완성 패킷이 남아 있어도 누적 버퍼의 남은 크기만큼만 수신한다.
    _recvEvent.wsaBuf.len = static_cast<ULONG>((std::min)(_recvBuffer.WritableSize(), static_cast<int32_t>(sizeof(_recvEvent.buffer))));
    if (_recvEvent.wsaBuf.len == 0)
        return false;

    DWORD flags = 0;
    int32_t result = WSARecv(_socket, &_recvEvent.wsaBuf, 1, nullptr, &flags, &_recvEvent.overlapped, nullptr);

    if (result == SOCKET_ERROR)
    {
        int32_t error = WSAGetLastError();

        // 비동기 Recv 요청이 정상적으로 대기 상태에 들어간 경우
        if (error != WSA_IO_PENDING)
            return false;
    }

    _recvPending = true;
    return true;
}

bool Session::Send(const char* data, int32_t size)
{
    if (!IsConnected())
        return false;

    if (data == nullptr || size <= 0)
        return false;

    // 느린 수신자의 대기열이 계속 늘어나지 않도록 연결별 송신량을 제한한다.
    // Player 제거는 Map 순회가 끝난 뒤 SessionManager에서 수행한다.
    if (static_cast<size_t>(size) > MaxQueuedSendBytes - _queuedSendBytes)
    {
        std::cerr << "Session send queue limit reached: queued=" << _queuedSendBytes << '\n';
        RequestClose();
        return false;
    }

    // 비동기 Send가 완료될 때까지 전송 데이터가 유효해야 하므로
    // 호출자가 넘긴 메모리를 직접 참조하지 않고 Session 소유 버퍼로 복사한다.
    SendBuffer sendBuffer;
    sendBuffer.buffer.assign(data, data + size);

    _sendQueue.push(std::move(sendBuffer));
    _queuedSendBytes += size;

    if (_sendPending)
        return true;

    if (!PostSend())
    {
        RequestClose();
        return false;
    }
    return true;
}

bool Session::Dispatch(IocpEvent* event, DWORD bytes, bool ioSuccess)
{
    if (event == nullptr)
        return false;

    switch (event->type)
    {
    case IocpEventType::Recv:
        _recvPending = false;

        // 종료 이후 도착한 성공 completion도 패킷 처리에 다시 사용하지 않는다.
        if (!IsConnected())
            return true;

        if (!ioSuccess)
        {
            Close();
            return true;
        }

        return OnRecv(bytes);

    case IocpEventType::Send:
        _sendPending = false;
        if (!IsConnected())
            return true;
        if (!ioSuccess)
        {
            _sendPending = false;
            Close();
            return true;
        }

        return OnSend(bytes);

    default:
        return false;
    }
}

bool Session::OnRecv(DWORD bytes)
{
    if (!IsConnected())
        return true;
    if (bytes == 0)
    {
        std::cout << "Client Disconnected\n";
        Close();
        return true;
    }

    if (bytes > static_cast<DWORD>(_recvBuffer.WritableSize()))
    {
        Close();
        return false;
    }

    memcpy(_recvBuffer.WritePos(), _recvEvent.buffer, bytes);

    if (!_recvBuffer.OnWrite(bytes))
    {
        Close();
        return false;
    }

    if (!ProcessPackets())
    {
        Close();
        return false;
    }

    if (!PostRecv())
    {
        Close();
        return false;
    }

    return true;
}

bool Session::PostSend()
{
    if (!IsConnected())
        return false;
    if (_sendQueue.empty())
        return true;

    SendBuffer& sendBuffer = _sendQueue.front();

    if (sendBuffer.sentBytes >= sendBuffer.buffer.size())
        return false;

    size_t remainingBytes = sendBuffer.buffer.size() - sendBuffer.sentBytes;

    _sendEvent.overlapped = {};
    _sendEvent.wsaBuf.buf = sendBuffer.buffer.data() + sendBuffer.sentBytes;
    _sendEvent.wsaBuf.len = static_cast<ULONG>(remainingBytes);

    // 이전 Send에서 일부만 전송됐을 수 있으므로
    // 아직 전송되지 않은 구간만 다음 WSASend에 넘김.

    DWORD sentBytes = 0;
    int32_t result = WSASend(_socket, &_sendEvent.wsaBuf, 1, &sentBytes, 0, &_sendEvent.overlapped, nullptr);

    if (result == SOCKET_ERROR)
    {
        int32_t error = WSAGetLastError();

        // WSA_IO_PENDING은 실패가 아니라 overlapped Recv가 정상적으로 등록되어
        // 이후 IOCP completion으로 완료될 예정이라는 의미
        if (error != WSA_IO_PENDING)
            return false;
    }

    _sendPending = true;
    return true;
}

bool Session::OnSend(DWORD bytes)
{
    _sendPending = false;

    if (_sendQueue.empty())
        return false;

    if (bytes == 0)
        return false;

    SendBuffer& sendBuffer = _sendQueue.front();

    size_t remainingBytes = sendBuffer.buffer.size() - sendBuffer.sentBytes;
    if (bytes > remainingBytes)
        return false;

    sendBuffer.sentBytes += bytes;
    _queuedSendBytes -= bytes;

    if (sendBuffer.sentBytes < sendBuffer.buffer.size())
        return PostSend();

    _sendQueue.pop();

    if (!_sendQueue.empty())
        return PostSend();

    return true;
}

// TCP는 애플리케이션 패킷 경계를 보존하지 않는다.
// 한 번의 Recv에 패킷 일부 또는 여러 패킷이 포함될 수 있으므로,
// PacketHeader::size만큼 데이터가 누적된 경우에만 하나의 패킷을 처리한다.
bool Session::ProcessPackets()
{
    while (true)
    {
        if (!IsConnected())
            return false;
        if (_recvBuffer.DataSize() < sizeof(PacketHeader))
            break;

        PacketHeader* header = reinterpret_cast<PacketHeader*>(_recvBuffer.ReadPos());

        if (header->size < sizeof(PacketHeader))
            return false;

        if (header->size > MAX_PACKET_SIZE)
            return false;

        if (_recvBuffer.DataSize() < header->size)
            break;

        uint16_t payloadSize = header->size - sizeof(PacketHeader);
        const char* payload = _recvBuffer.ReadPos() + sizeof(PacketHeader);

        if (!OnPacket(header->opcode, payload, payloadSize))
            return false;

        if (!_recvBuffer.OnRead(header->size))
            return false;

        _recvBuffer.Clean();
    }

    return true;
}
