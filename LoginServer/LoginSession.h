#pragma once

#include "../ServerCore/Session.h"
#include "../Protocol/LoginPacket.h"
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>

class AccountRepository;
class AuthKeyGenerator;
class CharacterRepository;
class GameServerClient;
class PasswordVerifier;
class RegistrationService;

// 로그인 연결 하나의 인증 상태를 보관한다.
// 외부 서비스/Repository는 소유하지 않고 reference로 참조하므로
// LoginSession보다 오래 유지되어야 한다.
class LoginSession : public Session
{
public:
    LoginSession(SOCKET socket, GameServerClient& gameServerClient, AuthKeyGenerator& authKeyGenerator, AccountRepository& accountRepository, CharacterRepository& characterRepository, PasswordVerifier& passwordVerifier, RegistrationService& registration);
    void Update() override;
    bool RegistrationPending() const { return _registrationResult.valid(); }
    bool AcceptRegistration();
    bool BeginRegistration(std::string id, std::string password);

    bool IsAuthenticated() const { return _authenticated; }
    void SetAuthenticated(bool authenticated) { _authenticated = authenticated; }

    uint32_t GetAccountId() const { return _accountId; }
    void SetAccountId(uint32_t accountId) { _accountId = accountId; }

    GameServerClient& GetGameServerClient() { return _gameServerClient; }
    AuthKeyGenerator& GetAuthKeyGenerator() { return _authKeyGenerator; }
    AccountRepository& GetAccountRepository() { return _accountRepository; }
    CharacterRepository& GetCharacterRepository() { return _characterRepository; }
    PasswordVerifier& GetPasswordVerifier() { return _passwordVerifier; }

protected:
    bool OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize) override;
    void OnDisconnected() override;

private:
    GameServerClient& _gameServerClient;
    AuthKeyGenerator& _authKeyGenerator;
    AccountRepository& _accountRepository;
    CharacterRepository& _characterRepository;
    PasswordVerifier& _passwordVerifier;
    RegistrationService& _registration;
    std::future<RegistrationResult> _registrationResult;
    std::shared_ptr<std::atomic<bool>> _registrationCanceled;
    std::chrono::steady_clock::time_point _nextRegistration{}, _registrationDeadline{};
    uint32_t _accountId = 0;
    bool _authenticated = false;
};
