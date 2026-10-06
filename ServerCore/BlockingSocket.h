#pragma once

#include <winsock2.h>
#include <chrono>
#include <algorithm>

namespace BlockingSocket
{
    using Deadline = std::chrono::steady_clock::time_point;

    inline bool Wait(SOCKET socket, bool writing, Deadline deadline)
    {
        auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::steady_clock::duration::zero())
            return false;

        auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(remaining).count();
        timeval timeout{ static_cast<long>(microseconds / 1000000), static_cast<long>(microseconds % 1000000) };
        fd_set sockets;
        FD_ZERO(&sockets);
        FD_SET(socket, &sockets);
        return select(0, writing ? nullptr : &sockets, writing ? &sockets : nullptr, nullptr, &timeout) > 0;
    }

    inline bool Connect(SOCKET socket, const sockaddr_in& address, Deadline deadline)
    {
        u_long nonblocking = 1;
        if (ioctlsocket(socket, FIONBIO, &nonblocking) == SOCKET_ERROR)
            return false;

        int result = connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
        if (result == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
            return false;
        if (result == SOCKET_ERROR && !Wait(socket, true, deadline))
            return false;

        int error = 0;
        int length = sizeof(error);
        if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) == SOCKET_ERROR || error != 0)
            return false;

        nonblocking = 0;
        return ioctlsocket(socket, FIONBIO, &nonblocking) != SOCKET_ERROR;
    }

    inline bool Transfer(SOCKET socket, char* buffer, int32_t size, bool writing, Deadline deadline)
    {
        int32_t offset = 0;
        while (offset < size)
        {
            if (!Wait(socket, writing, deadline))
                return false;

            // 일부 바이트만 계속 도착해도 전체 요청의 제한 시간을 연장하지 않는다.
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            DWORD timeout = static_cast<DWORD>((std::max)(1LL, static_cast<long long>(remaining)));
            if (setsockopt(socket, SOL_SOCKET, writing ? SO_SNDTIMEO : SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == SOCKET_ERROR)
                return false;

            int32_t bytes = writing ? send(socket, buffer + offset, size - offset, 0) : recv(socket, buffer + offset, size - offset, 0);
            if (bytes == SOCKET_ERROR || bytes == 0)
                return false;
            offset += bytes;
        }
        return true;
    }
}
