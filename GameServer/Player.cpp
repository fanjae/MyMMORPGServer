#include "Player.h"

#include <utility>

Player::Player(uint32_t characterId, uint32_t accountId, std::string name, uint16_t level)
    : _characterId(characterId), _accountId(accountId), _name(std::move(name)), _level(level)
{
}

bool Player::AcceptMoveSequence(uint64_t sequence)
{
    // Map을 변경해도 요청 번호를 유지해 이전 요청의 재사용을 막는다.
    if (sequence <= _lastMoveSequence)
        return false;

    _lastMoveSequence = sequence;
    return true;
}

void Player::BeginMap()
{
    ++_generation;
    _inputSequence = 0;
    _appliedInputSequence = 0;
    _appliedInputTicks = 0;
    _input = {};
    _jumpPending = false;
    _inputTime = std::chrono::steady_clock::now();
}

bool Player::AcceptInput(const MovementInputPacket& input)
{
    if (input.generation != _generation || input.sequence <= _inputSequence || input.horizontal < -1 || input.horizontal > 1 || input.jumpHeld > 1)
        return false;

    // 같은 tick 사이에 점프와 해제가 도착해도 짧은 점프 입력을 보존한다.
    if (input.jumpHeld != 0 && !_input.jumpHeld)
        _jumpPending = true;

    _inputSequence = input.sequence;
    _input.horizontal = input.horizontal;
    _input.jumpHeld = input.jumpHeld != 0;
    _inputTime = std::chrono::steady_clock::now();
    return true;
}

PlatformMovementInput Player::ConsumeInput(std::chrono::steady_clock::time_point now, bool& expired)
{
    expired = now - _inputTime >= std::chrono::milliseconds(250);
    if (expired)
    {
        _input = {};
        _jumpPending = false;
    }

    PlatformMovementInput input = _input;
    // 입력 번호만으로는 같은 입력이 이미 적용된 고정 단계 수를 구분할 수 없다.
    if (_appliedInputSequence != _inputSequence)
    {
        _appliedInputSequence = _inputSequence;
        _appliedInputTicks = 0;
    }
    if (_appliedInputTicks < UINT32_MAX)
        ++_appliedInputTicks;
    if (_jumpPending)
        _platformState.jumpHeld = false;
    input.jumpHeld = input.jumpHeld || _jumpPending;
    _jumpPending = false;
    return input;
}
