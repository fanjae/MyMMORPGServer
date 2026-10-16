#include "AccountRepository.h"
#include "AuthKeyGenerator.h"
#include "CharacterRepository.h"
#include "GameServerClient.h"
#include "LoginSession.h"
#include "LoginPacketHandler.h"
#include "PasswordVerifier.h"
#include "RegistrationService.h"

LoginSession::LoginSession(SOCKET socket, GameServerClient& gameServerClient, AuthKeyGenerator& authKeyGenerator, AccountRepository& accountRepository, CharacterRepository& characterRepository, PasswordVerifier& passwordVerifier, RegistrationService& registration) : Session(socket), _gameServerClient(gameServerClient), _authKeyGenerator(authKeyGenerator), _accountRepository(accountRepository), _characterRepository(characterRepository), _passwordVerifier(passwordVerifier), _registration(registration)
{
}

bool LoginSession::OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize)
{
    return LoginPacketHandler::Handle(*this, opcode, payload, payloadSize);
}

bool LoginSession::AcceptRegistration()
{
    auto now = std::chrono::steady_clock::now();
    if (IsAuthenticated() || RegistrationPending() || now < _nextRegistration) return false;
    _nextRegistration = now + std::chrono::seconds(2); return true;
}

bool LoginSession::BeginRegistration(std::string id, std::string password)
{
    _registrationCanceled = std::make_shared<std::atomic<bool>>(false);
    _registrationResult = _registration.Submit(std::move(id), std::move(password), _registrationCanceled);
    _registrationDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    return _registrationResult.valid() || LoginPacketHandler::SendRegistration(*this, RegistrationResult::Busy);
}

void LoginSession::Update()
{
    if (!RegistrationPending()) return;
    if (_registrationResult.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        auto result = RegistrationResult::ServerError;
        try { result = _registrationResult.get(); } catch (...) {}
        if (!LoginPacketHandler::SendRegistration(*this, result)) RequestClose();
    }
    else if (std::chrono::steady_clock::now() >= _registrationDeadline)
    {
        // 이미 커밋됐을 수도 있으므로 시간 초과를 가입 취소 성공으로 표시하지 않는다.
        _registrationCanceled->store(true); _registrationResult = {};
        if (!LoginPacketHandler::SendRegistration(*this, RegistrationResult::ServerError)) RequestClose();
    }
}

void LoginSession::OnDisconnected()
{
    if (_registrationCanceled) _registrationCanceled->store(true);
    _registrationResult = {};
}
