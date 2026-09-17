#pragma once

#include <winsock2.h>
#include <string>

class NetAddress
{
public:
    NetAddress() = default;
    NetAddress(const std::wstring& ip, uint16_t port);

    SOCKADDR_IN& GetAddress() { return _address; }
    const SOCKADDR_IN& GetAddress() const { return _address; }

private:
    SOCKADDR_IN _address = {};
};