#pragma once

#include <string>

class PasswordVerifier
{
public:
    bool Verify(const std::string& password, const std::string& passwordHash) const;
};