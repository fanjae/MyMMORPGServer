#include "../LoginServer/LoginPacket.h"
#include "../ServerCore/Packet.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>
#include <iostream>

#pragma comment(lib, "Ws2_32.lib")

namespace
{
    bool SendAll(SOCKET socket, const char* buffer, int32_t size)
    {
        int32_t totalSentBytes = 0;

        while (totalSentBytes < size)
        {
            int32_t sentBytes = send(socket, buffer + totalSentBytes, size - totalSentBytes, 0);

            if (sentBytes == SOCKET_ERROR || sentBytes == 0)
                return false;

            totalSentBytes += sentBytes;
        }

        return true;
    }

    bool RecvAll(SOCKET socket, char* buffer, int32_t size)
    {
        int32_t totalRecvBytes = 0;

        while (totalRecvBytes < size)
        {
            int32_t recvBytes = recv(socket, buffer + totalRecvBytes, size - totalRecvBytes, 0);

            if (recvBytes == SOCKET_ERROR || recvBytes == 0)
                return false;

            totalRecvBytes += recvBytes;
        }

        return true;
    }

    template<typename T>
    bool SendPacket(SOCKET socket, LoginPacketOpcode opcode, const T& payload)
    {
        PacketHeader header;
        header.size = sizeof(PacketHeader) + sizeof(T);
        header.opcode = static_cast<uint16_t>(opcode);

        char sendBuffer[sizeof(PacketHeader) + sizeof(T)];

        memcpy(sendBuffer, &header, sizeof(header));
        memcpy(sendBuffer + sizeof(header), &payload, sizeof(payload));

        return SendAll(socket, sendBuffer, static_cast<int32_t>(sizeof(sendBuffer)));
    }

    bool SendEmptyPacket(SOCKET socket, LoginPacketOpcode opcode)
    {
        PacketHeader header;
        header.size = sizeof(PacketHeader);
        header.opcode = static_cast<uint16_t>(opcode);

        return SendAll(socket, reinterpret_cast<const char*>(&header), sizeof(header));
    }

    template<typename T>
    bool RecvPacket(SOCKET socket, LoginPacketOpcode expectedOpcode, T& payload)
    {
        PacketHeader header;

        if (!RecvAll(socket, reinterpret_cast<char*>(&header), sizeof(header)))
            return false;

        if (header.opcode != static_cast<uint16_t>(expectedOpcode))
            return false;

        if (header.size != sizeof(PacketHeader) + sizeof(T))
            return false;

        return RecvAll(socket, reinterpret_cast<char*>(&payload), sizeof(payload));
    }
}

int main()
{
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return 1;

    SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (socket == INVALID_SOCKET)
    {
        WSACleanup();
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(7776);

    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1)
    {
        closesocket(socket);
        WSACleanup();
        return 1;
    }

    if (connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
    {
        std::cout << "Connect Failed\n";
        closesocket(socket);
        WSACleanup();
        return 1;
    }

    std::cout << "Connected to Login Server\n";

    LoginRequest loginRequest;
    loginRequest.accountId = 1;

    if (!SendPacket(socket, LoginPacketOpcode::LoginRequest, loginRequest))
    {
        std::cout << "Login Request Failed\n";
        closesocket(socket);
        WSACleanup();
        return 1;
    }

    LoginResponse loginResponse;

    if (!RecvPacket(socket, LoginPacketOpcode::LoginResponse, loginResponse))
    {
        std::cout << "Login Response Failed\n";
        closesocket(socket);
        WSACleanup();
        return 1;
    }

    std::cout << "Login Result: " << static_cast<int32_t>(loginResponse.result) << '\n';

    if (loginResponse.result != LoginResult::Success)
    {
        closesocket(socket);
        WSACleanup();
        return 1;
    }

    if (!SendEmptyPacket(socket, LoginPacketOpcode::CharacterListRequest))
    {
        std::cout << "Character List Request Failed\n";
        closesocket(socket);
        WSACleanup();
        return 1;
    }

    CharacterListResponse characterListResponse;

    if (!RecvPacket(socket, LoginPacketOpcode::CharacterListResponse, characterListResponse))
    {
        std::cout << "Character List Response Failed\n";
        closesocket(socket);
        WSACleanup();
        return 1;
    }

    std::cout << "Character Count: " << static_cast<int32_t>(characterListResponse.characterCount) << '\n';

    for (uint8_t i = 0; i < characterListResponse.characterCount; ++i)
    {
        const CharacterInfo& character = characterListResponse.characters[i];
        std::cout << "CharacterId: " << character.characterId << ", Name: " << character.name << ", Level: " << character.level << '\n';
    }

    CharacterSelectRequest characterSelectRequest;
    characterSelectRequest.characterId = 1001;

    if (!SendPacket(socket, LoginPacketOpcode::CharacterSelectRequest, characterSelectRequest))
    {
        std::cout << "Character Select Request Failed\n";
        closesocket(socket);
        WSACleanup();
        return 1;
    }

    CharacterSelectResponse characterSelectResponse;

    if (!RecvPacket(socket, LoginPacketOpcode::CharacterSelectResponse, characterSelectResponse))
    {
        std::cout << "Character Select Response Failed\n";
        closesocket(socket);
        WSACleanup();
        return 1;
    }

    std::cout << "Character Select Result: " << static_cast<int32_t>(characterSelectResponse.result) << '\n';

    if (characterSelectResponse.result == CharacterSelectResult::Success)
    {
        std::cout << "AuthKey: " << characterSelectResponse.authKey << '\n';
        std::cout << "GameServerPort: " << characterSelectResponse.gameServerPort << '\n';
    }

    closesocket(socket);
    WSACleanup();
    return 0;
}