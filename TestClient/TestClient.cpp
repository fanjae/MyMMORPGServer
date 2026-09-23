#include "../Protocol/GamePacket.h"
#include "../Protocol/LoginPacket.h"
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

    template<typename TOpcode, typename T>
    bool SendPacket(SOCKET socket, TOpcode opcode, const T& payload)
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

    template<typename TOpcode, typename T>
    bool RecvPacket(SOCKET socket, TOpcode expectedOpcode, T& payload)
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

    SOCKET loginSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (loginSocket == INVALID_SOCKET)
    {
        WSACleanup();
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(7776);

    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1)
    {
        closesocket(loginSocket);
        WSACleanup();
        return 1;
    }

    if (connect(loginSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
    {
        std::cout << "Connect Failed\n";
        closesocket(loginSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Connected to Login Server\n";

    LoginRequest loginRequest;
    strcpy_s(loginRequest.loginId, "test");
    strcpy_s(loginRequest.password, "test1234");

    if (!SendPacket(loginSocket, LoginPacketOpcode::LoginRequest, loginRequest))
    {
        std::cout << "Login Request Failed\n";
        closesocket(loginSocket);
        WSACleanup();
        return 1;
    }

    LoginResponse loginResponse;

    if (!RecvPacket(loginSocket, LoginPacketOpcode::LoginResponse, loginResponse))
    {
        std::cout << "Login Response Failed\n";
        closesocket(loginSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Login Result: " << static_cast<int32_t>(loginResponse.result) << '\n';

    if (loginResponse.result != LoginResult::Success)
    {
        closesocket(loginSocket);
        WSACleanup();
        return 1;
    }

    if (!SendEmptyPacket(loginSocket, LoginPacketOpcode::CharacterListRequest))
    {
        std::cout << "Character List Request Failed\n";
        closesocket(loginSocket);
        WSACleanup();
        return 1;
    }

    CharacterListResponse characterListResponse;

    if (!RecvPacket(loginSocket, LoginPacketOpcode::CharacterListResponse, characterListResponse))
    {
        std::cout << "Character List Response Failed\n";
        closesocket(loginSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Character List Result: " << static_cast<int32_t>(characterListResponse.result) << '\n';

    if (characterListResponse.result != CharacterListResult::Success)
    {
        closesocket(loginSocket);
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

    if (!SendPacket(loginSocket, LoginPacketOpcode::CharacterSelectRequest, characterSelectRequest))
    {
        std::cout << "Character Select Request Failed\n";
        closesocket(loginSocket);
        WSACleanup();
        return 1;
    }

    CharacterSelectResponse characterSelectResponse;

    if (!RecvPacket(loginSocket, LoginPacketOpcode::CharacterSelectResponse, characterSelectResponse))
    {
        std::cout << "Character Select Response Failed\n";
        closesocket(loginSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Character Select Result: " << static_cast<int32_t>(characterSelectResponse.result) << '\n';

    if (characterSelectResponse.result != CharacterSelectResult::Success)
    {
        closesocket(loginSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "AuthKey: " << characterSelectResponse.authKey << '\n';
    std::cout << "GameServerPort: " << characterSelectResponse.gameServerPort << '\n';

    closesocket(loginSocket);

    SOCKET gameSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (gameSocket == INVALID_SOCKET)
    {
        WSACleanup();
        return 1;
    }

    sockaddr_in gameAddress{};
    gameAddress.sin_family = AF_INET;
    gameAddress.sin_port = htons(characterSelectResponse.gameServerPort);

    if (inet_pton(AF_INET, "127.0.0.1", &gameAddress.sin_addr) != 1)
    {
        closesocket(gameSocket);
        WSACleanup();
        return 1;
    }

    if (connect(gameSocket, reinterpret_cast<sockaddr*>(&gameAddress), sizeof(gameAddress)) == SOCKET_ERROR)
    {
        std::cout << "Game Server Connect Failed\n";
        closesocket(gameSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Connected to Game Server\n";

    EnterGameRequest enterGameRequest;
    enterGameRequest.authKey = characterSelectResponse.authKey;

    if (!SendPacket(gameSocket, GamePacketOpcode::EnterGameRequest, enterGameRequest))
    {
        std::cout << "Enter Game Request Failed\n";
        closesocket(gameSocket);
        WSACleanup();
        return 1;
    }

    EnterGameResponse enterGameResponse;

    if (!RecvPacket(gameSocket, GamePacketOpcode::EnterGameResponse, enterGameResponse))
    {
        std::cout << "Enter Game Response Failed\n";
        closesocket(gameSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Enter Game Result: " << static_cast<int32_t>(enterGameResponse.result) << '\n';

    if (enterGameResponse.result != EnterGameResult::Success)
    {
        closesocket(gameSocket);
        WSACleanup();
        return 1;
    }

    closesocket(gameSocket);
    WSACleanup();
    return 0;
}