#pragma once
#include "LoginPacket.h"

// 가입 ID는 영문·숫자·밑줄만 허용하고 비밀번호의 공백과 대소문자는 그대로 유지한다.
inline bool IsValidRegistrationId(const std::string& id)
{
    if (id.size() < 3 || id.size() >= MAX_LOGIN_ID_LENGTH) return false;
    for (unsigned char value : id)
        if (!(value >= 'a' && value <= 'z') && !(value >= 'A' && value <= 'Z') &&
            !(value >= '0' && value <= '9') && value != '_') return false;
    return true;
}
inline bool IsValidRegistrationPassword(const std::string& password)
{
    if (password.size() >= MAX_PASSWORD_LENGTH) return false;
    uint32_t count = 0;
    for (size_t index = 0; index < password.size();)
    {
        uint32_t value;
        if (!ReadUtf8Scalar(password, index, value) || value < 0x20 || (value >= 0x7f && value <= 0x9f)) return false;
        ++count;
    }
    return count >= 8;
}
