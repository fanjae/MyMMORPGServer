#include "GamePacketHandler.h"
#include "GamePacket.h"
#include "GameSession.h"
#include "../ServerCore/Packet.h"

#include <cstring>
#include <iostream>

bool GamePacketHandler::Handle(GameSession& session, uint16_t opcode, const char* payload, uint16_t payloadSize)
{
    switch (static_cast<GamePacketOpcode>(opcode))
    {
    case GamePacketOpcode::EnterGameRequest:
        return HandleEnterGame(session, payload, payloadSize);

    default:
        return false;
    }
}

bool GamePacketHandler::HandleEnterGame(GameSession& session, const char* payload, uint16_t payloadSize)
{
    if (payloadSize != sizeof(EnterGameRequest))
        return false;

    EnterGameRequest request;
    memcpy(&request, payload, sizeof(request));

    EnterGameResponse response;

    if (session.IsAuthenticated())
    {
        response.result = EnterGameResult::AlreadyAuthenticated;
    }
    else if (request.authKey != 123456789)
    {
        response.result = EnterGameResult::InvalidAuthKey;
    }
    else
    {
        session.SetAuthenticated(true);
        response.result = EnterGameResult::Success;

        std::cout << "Game Session Authenticated\n";
    }

    PacketHeader header;
    header.size = sizeof(PacketHeader) + sizeof(EnterGameResponse);
    header.opcode = static_cast<uint16_t>(GamePacketOpcode::EnterGameResponse);

    char sendBuffer[sizeof(PacketHeader) + sizeof(EnterGameResponse)];

    memcpy(sendBuffer, &header, sizeof(header));
    memcpy(sendBuffer + sizeof(header), &response, sizeof(response));

    return session.Send(sendBuffer, sizeof(sendBuffer));
}