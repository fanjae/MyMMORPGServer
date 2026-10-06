#include "GameServerClient.h"
#include "../Protocol/ServerPacket.h"
#include "../ServerCore/NetAddress.h"
#include "../ServerCore/Packet.h"
#include "../ServerCore/SocketUtils.h"
#include "../ServerCore/BlockingSocket.h"

#include <cstring>

namespace
{
    bool RecvAll(SOCKET socket, char* buffer, int32_t size, BlockingSocket::Deadline deadline)
    {
        return BlockingSocket::Transfer(socket, buffer, size, false, deadline);
    }
}

// GameServer에 일회용 인증 티켓을 등록하고 등록 결과를 동기적으로 확인한다.
// 등록 성공 이후에만 LoginServer가 같은 authKey를 클라이언트에 전달한다.
bool GameServerClient::RegisterAuthTicket(uint32_t accountId, uint32_t characterId, uint64_t authKey)
{
    SOCKET socket = SocketUtils::CreateSocket();
    if (socket == INVALID_SOCKET)
        return false;

    NetAddress address(L"127.0.0.1", _port);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(_timeoutMs);

    if (!BlockingSocket::Connect(socket, address.GetAddress(), deadline))
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

    if (!BlockingSocket::Transfer(socket, sendBuffer, sizeof(sendBuffer), true, deadline))
    {
        SocketUtils::Close(socket);
        return false;
    }

    PacketHeader responseHeader;

    if (!RecvAll(socket, reinterpret_cast<char*>(&responseHeader), sizeof(responseHeader), deadline))
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

    if (!RecvAll(socket, reinterpret_cast<char*>(&response), sizeof(response), deadline))
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
