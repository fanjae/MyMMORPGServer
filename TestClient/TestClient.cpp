#include "../Protocol/GamePacket.h"
#include "../Protocol/LoginPacket.h"
#include "../ServerCore/Packet.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#pragma comment(lib, "Ws2_32.lib")

namespace
{
    constexpr const char* SERVER_ADDRESS = "127.0.0.1";
    constexpr uint16_t LOGIN_SERVER_PORT = 7776;

    struct CharacterTicket
    {
        uint64_t authKey = 0;
        uint16_t gameServerPort = 0;
        uint32_t characterId = 0;
    };

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

    SOCKET Connect(uint16_t port)
    {
        SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == INVALID_SOCKET)
            return INVALID_SOCKET;

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);

        if (inet_pton(AF_INET, SERVER_ADDRESS, &address.sin_addr) != 1)
        {
            closesocket(socket);
            return INVALID_SOCKET;
        }

        if (connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
        {
            closesocket(socket);
            return INVALID_SOCKET;
        }

        return socket;
    }

    bool CreateCharacterTicket(const char* loginId, const char* password, uint8_t characterIndex, CharacterTicket& ticket)
    {
        SOCKET socket = Connect(LOGIN_SERVER_PORT);
        if (socket == INVALID_SOCKET)
            return false;

        LoginRequest loginRequest;
        strcpy_s(loginRequest.loginId, loginId);
        strcpy_s(loginRequest.password, password);

        LoginResponse loginResponse;
        if (!SendPacket(socket, LoginPacketOpcode::LoginRequest, loginRequest) ||
            !RecvPacket(socket, LoginPacketOpcode::LoginResponse, loginResponse) ||
            loginResponse.result != LoginResult::Success)
        {
            closesocket(socket);
            return false;
        }

        CharacterListResponse listResponse;
        if (!SendEmptyPacket(socket, LoginPacketOpcode::CharacterListRequest) ||
            !RecvPacket(socket, LoginPacketOpcode::CharacterListResponse, listResponse) ||
            listResponse.result != CharacterListResult::Success ||
            characterIndex >= listResponse.characterCount)
        {
            closesocket(socket);
            return false;
        }

        CharacterSelectRequest selectRequest;
        selectRequest.characterId = listResponse.characters[characterIndex].characterId;

        CharacterSelectResponse selectResponse;
        if (!SendPacket(socket, LoginPacketOpcode::CharacterSelectRequest, selectRequest) ||
            !RecvPacket(socket, LoginPacketOpcode::CharacterSelectResponse, selectResponse) ||
            selectResponse.result != CharacterSelectResult::Success)
        {
            closesocket(socket);
            return false;
        }

        ticket.authKey = selectResponse.authKey;
        ticket.gameServerPort = selectResponse.gameServerPort;
        ticket.characterId = selectRequest.characterId;
        closesocket(socket);
        return true;
    }

    bool EnterGame(const CharacterTicket& ticket, EnterGameResult expectedResult, SOCKET& gameSocket)
    {
        gameSocket = Connect(ticket.gameServerPort);
        if (gameSocket == INVALID_SOCKET)
            return false;

        EnterGameRequest request;
        request.authKey = ticket.authKey;

        EnterGameResponse response;
        if (!SendPacket(gameSocket, GamePacketOpcode::EnterGameRequest, request) ||
            !RecvPacket(gameSocket, GamePacketOpcode::EnterGameResponse, response))
        {
            closesocket(gameSocket);
            gameSocket = INVALID_SOCKET;
            return false;
        }

        if (response.result != expectedResult)
        {
            std::cout << "Unexpected EnterGameResult. expected=" << static_cast<int32_t>(expectedResult)
                << " actual=" << static_cast<int32_t>(response.result) << '\n';
            closesocket(gameSocket);
            gameSocket = INVALID_SOCKET;
            return false;
        }

        if (expectedResult == EnterGameResult::Success && response.characterId != ticket.characterId)
        {
            closesocket(gameSocket);
            gameSocket = INVALID_SOCKET;
            return false;
        }

        if (expectedResult != EnterGameResult::Success)
        {
            closesocket(gameSocket);
            gameSocket = INVALID_SOCKET;
        }

        return true;
    }

    bool TestDuplicateEntry(const char* loginId, const char* password, uint8_t characterIndex)
    {
        CharacterTicket ticket;
        if (!CreateCharacterTicket(loginId, password, characterIndex, ticket))
            return false;

        SOCKET socket = INVALID_SOCKET;
        return EnterGame(ticket, EnterGameResult::AlreadyInGame, socket);
    }

    bool RecvPlayerEnter(SOCKET socket, uint32_t expectedCharacterId)
    {
        PlayerEnterMap playerEnter;
        if (!RecvPacket(socket, GamePacketOpcode::PlayerEnterMap, playerEnter))
            return false;

        return playerEnter.characterId == expectedCharacterId;
    }

    bool SendMove(SOCKET socket, int32_t x, int32_t y)
    {
        MoveRequest request;
        request.x = x;
        request.y = y;
        return SendPacket(socket, GamePacketOpcode::MoveRequest, request);
    }

    bool RecvPlayerMove(SOCKET socket, uint32_t expectedCharacterId, int32_t expectedX, int32_t expectedY)
    {
        PlayerMove playerMove;
        if (!RecvPacket(socket, GamePacketOpcode::PlayerMove, playerMove))
            return false;

        return playerMove.characterId == expectedCharacterId && playerMove.x == expectedX && playerMove.y == expectedY;
    }

    bool RecvPlayerLeave(SOCKET socket, uint32_t expectedCharacterId)
    {
        PlayerLeaveMap playerLeave;
        if (!RecvPacket(socket, GamePacketOpcode::PlayerLeaveMap, playerLeave))
            return false;

        return playerLeave.characterId == expectedCharacterId;
    }

    bool ReenterAfterDisconnect(const char* loginId, const char* password, uint8_t characterIndex, SOCKET& gameSocket)
    {
        constexpr int32_t MAX_ATTEMPTS = 20;

        for (int32_t attempt = 0; attempt < MAX_ATTEMPTS; ++attempt)
        {
            CharacterTicket ticket;
            if (!CreateCharacterTicket(loginId, password, characterIndex, ticket))
                return false;

            SOCKET socket = Connect(ticket.gameServerPort);
            if (socket == INVALID_SOCKET)
                return false;

            EnterGameRequest request;
            request.authKey = ticket.authKey;

            EnterGameResponse response;
            if (!SendPacket(socket, GamePacketOpcode::EnterGameRequest, request) ||
                !RecvPacket(socket, GamePacketOpcode::EnterGameResponse, response))
            {
                closesocket(socket);
                return false;
            }

            if (response.result == EnterGameResult::Success)
            {
                if (response.characterId != ticket.characterId)
                {
                    closesocket(socket);
                    return false;
                }

                gameSocket = socket;
                return true;
            }

            closesocket(socket);

            if (response.result != EnterGameResult::AlreadyInGame)
                return false;

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        return false;
    }
}

int main()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return 1;

    SOCKET playerASocket = INVALID_SOCKET;
    SOCKET playerBSocket = INVALID_SOCKET;
    SOCKET reenteredPlayerASocket = INVALID_SOCKET;

    CharacterTicket playerATicket;
    CharacterTicket playerBTicket;

    bool success = CreateCharacterTicket("test", "test1234", 0, playerATicket) &&
        EnterGame(playerATicket, EnterGameResult::Success, playerASocket);

    if (success)
        std::cout << "[PASS] Player A entered the game\n";

    success = success && CreateCharacterTicket("test2", "test1234", 0, playerBTicket) &&
        EnterGame(playerBTicket, EnterGameResult::Success, playerBSocket);

    if (success)
        std::cout << "[PASS] Player B entered while Player A remained connected\n";

    success = success &&
        RecvPlayerEnter(playerASocket, playerBTicket.characterId) &&
        RecvPlayerEnter(playerBSocket, playerATicket.characterId);

    if (success)
        std::cout << "[PASS] Players received each other's map entry\n";

    success = success && SendMove(playerASocket, 120, 45) &&
        RecvPlayerMove(playerBSocket, playerATicket.characterId, 120, 45);

    if (success)
        std::cout << "[PASS] Player B received Player A's movement\n";

    success = success && TestDuplicateEntry("test", "test1234", 0);
    if (success)
        std::cout << "[PASS] Duplicate character entry was rejected\n";

    success = success && TestDuplicateEntry("test", "test1234", 1);
    if (success)
        std::cout << "[PASS] Same-account different-character entry was rejected\n";

    if (playerASocket != INVALID_SOCKET)
    {
        closesocket(playerASocket);
        playerASocket = INVALID_SOCKET;
    }

    success = success && RecvPlayerLeave(playerBSocket, playerATicket.characterId);
    if (success)
        std::cout << "[PASS] Player B received Player A's map leave\n";

    success = success && ReenterAfterDisconnect("test", "test1234", 0, reenteredPlayerASocket);
    if (success)
        std::cout << "[PASS] Player A re-entered after disconnect cleanup\n";

    success = success &&
        RecvPlayerEnter(playerBSocket, playerATicket.characterId) &&
        RecvPlayerEnter(reenteredPlayerASocket, playerBTicket.characterId);

    if (success)
        std::cout << "[PASS] Players received map entry after Player A re-entered\n";

    if (reenteredPlayerASocket != INVALID_SOCKET)
        closesocket(reenteredPlayerASocket);

    if (playerBSocket != INVALID_SOCKET)
        closesocket(playerBSocket);

    WSACleanup();

    if (!success)
    {
        std::cout << "Multi-client player lifecycle test failed\n";
        return 1;
    }

    std::cout << "Multi-client player lifecycle test succeeded\n";
    return 0;
}
