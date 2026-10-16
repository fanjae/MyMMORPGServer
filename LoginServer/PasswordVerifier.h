#pragma once

#include <string>

class PasswordVerifier
{
public:
    bool CreateHash(const std::string& password, std::string& passwordHash) const;
    bool Verify(const std::string& password, const std::string& passwordHash) const;
};
