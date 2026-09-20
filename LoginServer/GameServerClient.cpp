#include "GameServerClient.h"
#include "../Protocol/ServerPacket.h"
#include "../ServerCore/NetAddress.h"
#include "../ServerCore/Packet.h"
#include "../ServerCore/SocketUtils.h"

#include <cstring>

namespace
{
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
}

bool GameServerClient::RegisterAuthTicket(uint32_t accountId, uint32_t characterId, uint64_t authKey)
{
    SOCKET socket = SocketUtils::CreateSocket();
    if (socket == INVALID_SOCKET)
        return false;

    NetAddress address(L"127.0.0.1", 7778);

    if (!SocketUtils::Connect(socket, address))
    {
        SocketUtils::Close(socket);
        return false;
    }

    RegisterAuthTicketRequest request;
    request.accountId = accountId;
    request.characterId = characterId;
    request.authKey = authKey;

    PacketHeader header;
    header.size = sizeof(PacketHeader) + sizeof(RegisterAuthTicketRequest);
    header.opcode = static_cast<uint16_t>(ServerPacketOpcode::RegisterAuthTicketRequest);

    char sendBuffer[sizeof(PacketHeader) + sizeof(RegisterAuthTicketRequest)];

    memcpy(sendBuffer, &header, sizeof(header));
    memcpy(sendBuffer + sizeof(header), &request, sizeof(request));

    int32_t totalSentBytes = 0;
    int32_t sendBufferSize = static_cast<int32_t>(sizeof(sendBuffer));

    while (totalSentBytes < sendBufferSize)
    {
        int32_t sentBytes = send(socket, sendBuffer + totalSentBytes, sendBufferSize - totalSentBytes, 0);

        if (sentBytes == SOCKET_ERROR || sentBytes == 0)
        {
            SocketUtils::Close(socket);
            return false;
        }

        totalSentBytes += sentBytes;
    }

    PacketHeader responseHeader;

    if (!RecvAll(socket, reinterpret_cast<char*>(&responseHeader), sizeof(responseHeader)))
    {
        SocketUtils::Close(socket);
        return false;
    }

    if (responseHeader.size != sizeof(PacketHeader) + sizeof(RegisterAuthTicketResponse))
    {
        SocketUtils::Close(socket);
        return false;
    }

    if (responseHeader.opcode != static_cast<uint16_t>(ServerPacketOpcode::RegisterAuthTicketResponse))
    {
        SocketUtils::Close(socket);
        return false;
    }

    RegisterAuthTicketResponse response;

    if (!RecvAll(socket, reinterpret_cast<char*>(&response), sizeof(response)))
    {
        SocketUtils::Close(socket);
        return false;
    }

    SocketUtils::Close(socket);

    if (response.authKey != authKey)
        return false;

    if (response.result != RegisterAuthTicketResult::Success)
        return false;

    return true;
}