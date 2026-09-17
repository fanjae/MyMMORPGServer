#include "pch.h"
#include "NetAddress.h"

NetAddress::NetAddress(const std::wstring& ip, uint16_t port)
{
    _address.sin_family = AF_INET;
    _address.sin_port = htons(port);

    // 문자열 형태의 IPv4 주소를 네트워크 주소 형식으로 변환
    InetPtonW(AF_INET, ip.c_str(), &_address.sin_addr);
}