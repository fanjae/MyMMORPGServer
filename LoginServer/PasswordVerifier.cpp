#include "PasswordVerifier.h"

#include <Windows.h>
#include <bcrypt.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace
{
    // DB password_hash 형식은 pbkdf2-sha256 형태.
    constexpr uint32_t PBKDF2_HASH_SIZE = 32;
    constexpr const char* PBKDF2_ALGORITHM = "pbkdf2-sha256";

    bool HexCharacterToValue(char character, uint8_t& value)
    {
        if (character >= '0' && character <= '9')
        {
            value = static_cast<uint8_t>(character - '0');
            return true;
        }

        if (character >= 'a' && character <= 'f')
        {
            value = static_cast<uint8_t>(character - 'a' + 10);
            return true;
        }

        if (character >= 'A' && character <= 'F')
        {
            value = static_cast<uint8_t>(character - 'A' + 10);
            return true;
        }

        return false;
    }

    bool DecodeHex(const std::string& hex, std::vector<uint8_t>& bytes)
    {
        if (hex.empty() || hex.size() % 2 != 0)
            return false;

        bytes.clear();
        bytes.reserve(hex.size() / 2);

        for (size_t i = 0; i < hex.size(); i += 2)
        {
            uint8_t high = 0;
            uint8_t low = 0;

            if (!HexCharacterToValue(hex[i], high))
                return false;

            if (!HexCharacterToValue(hex[i + 1], low))
                return false;

            bytes.push_back(static_cast<uint8_t>((high << 4) | low));
        }

        return true;
    }

    // 해시 내용에 따라 비교가 중간에 종료되지 않도록 모든 바이트를 비교
    bool ConstantTimeEquals(const std::vector<uint8_t>& lhs, const std::vector<uint8_t>& rhs)
    {
        if (lhs.size() != rhs.size())
            return false;

        uint8_t difference = 0;

        for (size_t i = 0; i < lhs.size(); ++i)
            difference |= lhs[i] ^ rhs[i];

        return difference == 0;
    }

    // 저장된 password_hash를 검증하면서 PBKDF2 파라미터와 salt/hash를 추출
    bool ParseIterations(const std::string& text, uint64_t& iterations)
    {
        if (text.empty())
            return false;

        char* end = nullptr;
        const unsigned long long value = std::strtoull(text.c_str(), &end, 10);

        if (end == text.c_str() || *end != '\0')
            return false;

        if (value == 0)
            return false;

        iterations = static_cast<uint64_t>(value);
        return true;
    }

    bool ParsePasswordHash(const std::string& passwordHash, uint64_t& iterations, std::vector<uint8_t>& salt, std::vector<uint8_t>& storedHash)
    {
        const size_t firstSeparator = passwordHash.find('$');

        if (firstSeparator == std::string::npos)
            return false;

        const size_t secondSeparator = passwordHash.find('$', firstSeparator + 1);

        if (secondSeparator == std::string::npos)
            return false;

        const size_t thirdSeparator = passwordHash.find('$', secondSeparator + 1);

        if (thirdSeparator == std::string::npos)
            return false;

        if (passwordHash.find('$', thirdSeparator + 1) != std::string::npos)
            return false;

        const std::string algorithm = passwordHash.substr(0, firstSeparator);
        const std::string iterationsText = passwordHash.substr(firstSeparator + 1, secondSeparator - firstSeparator - 1);
        const std::string saltHex = passwordHash.substr(secondSeparator + 1, thirdSeparator - secondSeparator - 1);
        const std::string hashHex = passwordHash.substr(thirdSeparator + 1);

        if (algorithm != PBKDF2_ALGORITHM)
            return false;

        if (!ParseIterations(iterationsText, iterations))
            return false;

        if (!DecodeHex(saltHex, salt))
            return false;

        if (!DecodeHex(hashHex, storedHash))
            return false;

        if (salt.empty())
            return false;

        if (storedHash.size() != PBKDF2_HASH_SIZE)
            return false;

        return true;
    }
}

// PBKDF2-HMAC-SHA256 계산에 사용할 SHA-256 HMAC provider를 연다.
bool PasswordVerifier::Verify(const std::string& password, const std::string& passwordHash) const
{
    if (password.empty() || passwordHash.empty())
        return false;

    uint64_t iterations = 0;
    std::vector<uint8_t> salt;
    std::vector<uint8_t> storedHash;

    if (!ParsePasswordHash(passwordHash, iterations, salt, storedHash))
        return false;

    BCRYPT_ALG_HANDLE algorithmHandle = nullptr;

    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithmHandle, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);

    if (status < 0)
        return false;

    std::vector<uint8_t> derivedHash(PBKDF2_HASH_SIZE);

    status = BCryptDeriveKeyPBKDF2(algorithmHandle,reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),static_cast<ULONG>(password.size()),salt.data(),static_cast<ULONG>(salt.size()),iterations,derivedHash.data(),static_cast<ULONG>(derivedHash.size()),0);

    BCryptCloseAlgorithmProvider(algorithmHandle, 0);

    if (status < 0)
        return false;

    return ConstantTimeEquals(derivedHash, storedHash);
}