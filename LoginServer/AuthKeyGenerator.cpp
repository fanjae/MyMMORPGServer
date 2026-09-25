#include "AuthKeyGenerator.h"

#include <Windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

bool AuthKeyGenerator::Generate(uint64_t& authKey)
{
    // // authKey 0은 미발급/유효하지 않은 값을 나타내는 sentinel로 사용하므로 제외
    do
    {
        NTSTATUS status = BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&authKey), sizeof(authKey), BCRYPT_USE_SYSTEM_PREFERRED_RNG);

        if (status < 0)
            return false;
    } while (authKey == 0);

    return true;
}