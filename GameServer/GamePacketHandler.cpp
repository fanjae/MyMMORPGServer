#include "AuthTicketManager.h"
#include "GamePacketHandler.h"
#include "GameSession.h"
#include "../Protocol/GamePacket.h"
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
    else
    {
        AuthTicket ticket;

        // LoginServer가 선등록한 일회용 authKey를 소비
        // 인증에 성공하면 클라이언트가 보낸 식별자가 아닌
        // 서버가 보관한 티켓의 accountId/characterId를 Session에 바인딩
        if (!session.GetAuthTicketManager().Consume(request.authKey, ticket))
        {
            response.result = EnterGameResult::InvalidAuthKey;
        }
        else
        {
            session.SetAccountId(ticket.accountId);
            session.SetCharacterId(ticket.characterId);
            session.SetAuthenticated(true);
            response.result = EnterGameResult::Success;

            std::cout << "Game Session Authenticated: accountId=" << ticket.accountId << " characterId=" << ticket.characterId << "\n";
        }
    }

    PacketHeader header;
    header.size = sizeof(PacketHeader) + sizeof(EnterGameResponse);
    header.opcode = static_cast<uint16_t>(GamePacketOpcode::EnterGameResponse);

    char sendBuffer[sizeof(PacketHeader) + sizeof(EnterGameResponse)];

    memcpy(sendBuffer, &header, sizeof(header));
    memcpy(sendBuffer + sizeof(header), &response, sizeof(response));

    return session.Send(sendBuffer, sizeof(sendBuffer));
}