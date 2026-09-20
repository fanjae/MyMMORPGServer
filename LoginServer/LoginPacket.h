#pragma once

#include <cstdint>

constexpr uint32_t MAX_CHARACTER_NAME_LENGTH = 16;
constexpr uint32_t MAX_CHARACTER_COUNT = 3;

enum class LoginPacketOpcode : uint16_t
{
    LoginRequest = 1,
    LoginResponse = 2,
    CharacterListRequest = 3,
    CharacterListResponse = 4,
    CharacterSelectRequest = 5,
    CharacterSelectResponse = 6
};

enum class LoginResult : uint8_t
{
    Success = 0,
    InvalidAccount = 1
};

enum class CharacterSelectResult : uint8_t
{
    Success = 0,
    InvalidCharacter = 1,
    ServerUnavailable = 2
};

#pragma pack(push, 1)

struct LoginRequest
{
    uint32_t accountId = 0;
};

struct LoginResponse
{
    LoginResult result = LoginResult::InvalidAccount;
};

struct CharacterInfo
{
    uint32_t characterId = 0;
    char name[MAX_CHARACTER_NAME_LENGTH] = {};
    uint16_t level = 1;
};

struct CharacterListResponse
{
    uint8_t characterCount = 0;
    CharacterInfo characters[MAX_CHARACTER_COUNT] = {};
};

struct CharacterSelectRequest
{
    uint32_t characterId = 0;
};

struct CharacterSelectResponse
{
    CharacterSelectResult result = CharacterSelectResult::InvalidCharacter;
    uint64_t authKey = 0;
    uint16_t gameServerPort = 0;
};

#pragma pack(pop)