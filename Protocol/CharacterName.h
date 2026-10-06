#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include "Utf8String.h"

// DB의 최대 16문자를 UTF-8의 최대 4바이트와 종료 문자까지 포함해 전달한다.
constexpr uint32_t MAX_CHARACTER_NAME_CHARACTERS = 16;
constexpr uint32_t MAX_CHARACTER_NAME_LENGTH = MAX_CHARACTER_NAME_CHARACTERS * 4 + 1;

inline bool IsValidCharacterName(const std::string& name)
{
    if (name.empty() || name.size() >= MAX_CHARACTER_NAME_LENGTH)
        return false;
    uint32_t characters = 0;
    for (size_t index = 0; index < name.size();)
    {
        uint32_t codepoint;
        if (!ReadUtf8Scalar(name, index, codepoint) || codepoint < 0x20 || codepoint == 0x7f || ++characters > MAX_CHARACTER_NAME_CHARACTERS)
            return false;
    }
    return true;
}

inline bool CopyCharacterName(char (&destination)[MAX_CHARACTER_NAME_LENGTH], const std::string& name)
{
    if (!IsValidCharacterName(name))
        return false;

    memset(destination, 0, sizeof(destination));
    memcpy(destination, name.data(), name.size());
    return true;
}
