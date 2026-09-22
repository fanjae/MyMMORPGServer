#include "AccountRepository.h"
#include "AuthKeyGenerator.h"
#include "CharacterRepository.h"
#include "GameServerClient.h"
#include "LoginPacketHandler.h"
#include "LoginPacket.h"
#include "LoginSession.h"
#include "../ServerCore/Packet.h"

#include <algorithm>
#include <cstring>
#include <vector>

bool LoginPacketHandler::Handle(LoginSession& session, uint16_t opcode, const char* payload, uint16_t payloadSize)
{
    switch (static_cast<LoginPacketOpcode>(opcode))
    {
    case LoginPacketOpcode::LoginRequest:
        return HandleLogin(session, payload, payloadSize);

    case LoginPacketOpcode::CharacterListRequest:
        return HandleCharacterList(session, payload, payloadSize);

    case LoginPacketOpcode::CharacterSelectRequest:
        return HandleCharacterSelect(session, payload, payloadSize);

    default:
        return false;
    }
}

bool LoginPacketHandler::HandleLogin(LoginSession& session, const char* payload, uint16_t payloadSize)
{
    if (payloadSize != sizeof(LoginRequest))
        return false;

    LoginRequest request;
    memcpy(&request, payload, sizeof(request));

    LoginResponse response;

    if (!session.GetAccountRepository().ExistsById(request.accountId))
    {
        response.result = LoginResult::InvalidAccount;
    }
    else
    {
        session.SetAuthenticated(true);
        session.SetAccountId(request.accountId);
        response.result = LoginResult::Success;
    }

    PacketHeader header;
    header.size = sizeof(PacketHeader) + sizeof(LoginResponse);
    header.opcode = static_cast<uint16_t>(LoginPacketOpcode::LoginResponse);

    char sendBuffer[sizeof(PacketHeader) + sizeof(LoginResponse)];

    memcpy(sendBuffer, &header, sizeof(header));
    memcpy(sendBuffer + sizeof(header), &response, sizeof(response));

    return session.Send(sendBuffer, sizeof(sendBuffer));
}

bool LoginPacketHandler::HandleCharacterList(LoginSession& session, const char* payload, uint16_t payloadSize)
{
    if (payloadSize != 0)
        return false;

    if (!session.IsAuthenticated())
        return false;

    const std::vector<CharacterInfo> characters = session.GetCharacterRepository().FindByAccountId(session.GetAccountId());

    CharacterListResponse response;
    response.characterCount = static_cast<uint8_t>((std::min)(characters.size(), static_cast<size_t>(MAX_CHARACTER_COUNT)));

    for (size_t i = 0; i < response.characterCount; ++i)
        response.characters[i] = characters[i];

    PacketHeader header;
    header.size = sizeof(PacketHeader) + sizeof(CharacterListResponse);
    header.opcode = static_cast<uint16_t>(LoginPacketOpcode::CharacterListResponse);

    char sendBuffer[sizeof(PacketHeader) + sizeof(CharacterListResponse)];

    memcpy(sendBuffer, &header, sizeof(header));
    memcpy(sendBuffer + sizeof(header), &response, sizeof(response));

    return session.Send(sendBuffer, sizeof(sendBuffer));
}

bool LoginPacketHandler::HandleCharacterSelect(LoginSession& session, const char* payload, uint16_t payloadSize)
{
    if (payloadSize != sizeof(CharacterSelectRequest))
        return false;

    if (!session.IsAuthenticated())
        return false;

    CharacterSelectRequest request;
    memcpy(&request, payload, sizeof(request));

    CharacterSelectResponse response;

    if (!session.GetCharacterRepository().ExistsByAccountIdAndCharacterId(session.GetAccountId(), request.characterId))
    {
        response.result = CharacterSelectResult::InvalidCharacter;
    }
    else
    {
        uint64_t authKey = 0;

        if (!session.GetAuthKeyGenerator().Generate(authKey))
            return false;

        if (!session.GetGameServerClient().RegisterAuthTicket(session.GetAccountId(), request.characterId, authKey))
        {
            response.result = CharacterSelectResult::ServerUnavailable;
        }
        else
        {
            response.result = CharacterSelectResult::Success;
            response.authKey = authKey;
            response.gameServerPort = 7777;
        }
    }

    PacketHeader header;
    header.size = sizeof(PacketHeader) + sizeof(CharacterSelectResponse);
    header.opcode = static_cast<uint16_t>(LoginPacketOpcode::CharacterSelectResponse);

    char sendBuffer[sizeof(PacketHeader) + sizeof(CharacterSelectResponse)];

    memcpy(sendBuffer, &header, sizeof(header));
    memcpy(sendBuffer + sizeof(header), &response, sizeof(response));

    return session.Send(sendBuffer, sizeof(sendBuffer));
}