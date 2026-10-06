#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdlib>
#include <string>
#include <utility>

struct ServerListenConfig
{
    std::string clientBindIp = "127.0.0.1";

    static bool TryParse(const char* value, ServerListenConfig& config, std::string& error)
    {
        // 외부 테스트는 명시한 주소에서만 받고 기존 로컬 테스트의 기본값은 유지한다.
        std::string ip = value == nullptr ? "127.0.0.1" : value;
        IN_ADDR address{};
        if (InetPtonA(AF_INET, ip.c_str(), &address) != 1)
        {
            error = "SERVER_BIND_IP must be an IPv4 address, for example 127.0.0.1 or 0.0.0.0";
            return false;
        }
        config.clientBindIp = std::move(ip);
        error.clear();
        return true;
    }

    static bool Load(ServerListenConfig& config, std::string& error)
    {
        char* value = nullptr;
        size_t length = 0;
        if (_dupenv_s(&value, &length, "SERVER_BIND_IP") != 0)
        {
            error = "SERVER_BIND_IP could not be read";
            return false;
        }
        bool valid = TryParse(value, config, error);
        free(value);
        return valid;
    }

    std::wstring GetClientBindIp() const { return std::wstring(clientBindIp.begin(), clientBindIp.end()); }
};
