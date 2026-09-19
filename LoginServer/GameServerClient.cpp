#include "GameServerClient.h"
#include "../Protocol/ServerPacket.h"
#include "../ServerCore/NetAddress.h"
#include "../ServerCore/Packet.h"
#include "../ServerCore/SocketUtils.h"

#include <cstring>

bool GameServerClient::RegisterAuthTicket(uint32_t accountId, uint64_t authKey)
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
    request.authKey = authKey;

    PacketHeader header;
    header.size = sizeof(PacketHeader) + sizeof(RegisterAuthTicketRequest);
    header.opcode = static_cast<uint16_t>(ServerPacketOpcode::RegisterAuthTicket);

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

    SocketUtils::Close(socket);
    return true;
}