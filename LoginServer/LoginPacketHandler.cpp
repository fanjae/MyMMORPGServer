#include "AuthKeyGenerator.h"
#include "GameServerClient.h"
#include "LoginPacketHandler.h"
#include "LoginPacket.h"
#include "LoginSession.h"
#include "../ServerCore/Packet.h"

#include <array>
#include <cstring>

namespace
{
    std::array<CharacterInfo, 2> CreateCharacters()
    {
        std::array<CharacterInfo, 2> characters{};

        characters[0].characterId = 1001;
        memcpy(characters[0].name, "Warrior", strlen("Warrior"));
        characters[0].level = 10;

        characters[1].characterId = 1002;
        memcpy(characters[1].name, "Magician", strlen("Magician"));
        characters[1].level = 15;

        return characters;
    }

    const std::array<CharacterInfo, 2> characters = CreateCharacters();

    bool IsValidCharacter(uint32_t accountId, uint32_t characterId)
    {
        if (accountId != 1)
            return false;

        for (const CharacterInfo& character : characters)
        {
            if (character.characterId == characterId)
                return true;
        }

        return false;
    }
}

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

    if (request.accountId != 1)
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

    CharacterListResponse response;
    response.characterCount = static_cast<uint8_t>(characters.size());

    for (size_t i = 0; i < characters.size(); ++i)
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

    if (!IsValidCharacter(session.GetAccountId(), request.characterId))
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