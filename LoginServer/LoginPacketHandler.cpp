#include "AccountRepository.h"
#include "AuthKeyGenerator.h"
#include "CharacterRepository.h"
#include "GameServerClient.h"
#include "LoginPacketHandler.h"
#include "LoginSession.h"
#include "PasswordVerifier.h"
#include "RepositoryResult.h"
#include "../Protocol/LoginPacket.h"
#include "../ServerCore/Packet.h"

#include <algorithm>
#include <cstring>
#include <string>
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

    const size_t loginIdLength = strnlen_s(request.loginId, MAX_LOGIN_ID_LENGTH);
    const size_t passwordLength = strnlen_s(request.password, MAX_PASSWORD_LENGTH);

    if (loginIdLength == MAX_LOGIN_ID_LENGTH || passwordLength == MAX_PASSWORD_LENGTH)
        return false;

    const std::string loginId(request.loginId, loginIdLength);
    const std::string password(request.password, passwordLength);

    if (loginId.empty() || password.empty())
    {
        response.result = LoginResult::InvalidCredential;
    }
    else
    {
        const AccountQueryResult queryResult = session.GetAccountRepository().FindByLoginId(loginId);

        if (queryResult.status == RepositoryStatus::DatabaseError)
        {
            response.result = LoginResult::ServerError;
        }
        else if (queryResult.status == RepositoryStatus::NotFound)
        {
            response.result = LoginResult::InvalidCredential;
        }
        else if (!session.GetPasswordVerifier().Verify(password, queryResult.account.passwordHash))
        {
            response.result = LoginResult::InvalidCredential;
        }
        else
        {
            session.SetAuthenticated(true);
            session.SetAccountId(queryResult.account.accountId);
            response.result = LoginResult::Success;
        }
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

    const CharacterListQueryResult queryResult = session.GetCharacterRepository().FindByAccountId(session.GetAccountId());

    CharacterListResponse response;

    if (queryResult.status == RepositoryStatus::DatabaseError)
    {
        response.result = CharacterListResult::ServerError;
    }
    else
    {
        response.result = CharacterListResult::Success;
        response.characterCount = static_cast<uint8_t>((std::min)(queryResult.characters.size(), static_cast<size_t>(MAX_CHARACTER_COUNT)));

        for (size_t i = 0; i < response.characterCount; ++i)
            response.characters[i] = queryResult.characters[i];
    }

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

    const RepositoryStatus repositoryStatus = session.GetCharacterRepository().ExistsByAccountIdAndCharacterId(session.GetAccountId(), request.characterId);

    if (repositoryStatus == RepositoryStatus::NotFound)
    {
        response.result = CharacterSelectResult::InvalidCharacter;
    }
    else if (repositoryStatus == RepositoryStatus::DatabaseError)
    {
        response.result = CharacterSelectResult::ServerUnavailable;
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