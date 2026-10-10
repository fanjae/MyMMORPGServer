#include "Player.h"

#include <utility>
#include <cmath>

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
    _actionSequence = _acceptedActionSequence = _batchSequence = _latestClientTick = _actionTick = _jumpId = 0;
    _airborne = false;
    _platformState = {};
    _state3D = {};
}

bool Player::AcceptAction3D(const MovementAction3D& action)
{
    if (action.sequence <= _actionSequence || action.clientTick < _actionTick) return false;
    // 거절한 점프 번호도 소비해 착지 이후 같은 요청이 다시 실행되지 않게 한다.
    _actionSequence = action.sequence;
    switch (action.kind)
    {
    case MovementActionKind::Jump:
    case MovementActionKind::Fall:
        if (_airborne || !_state3D.grounded || action.grounded || action.jumpId <= _jumpId) return false;
        _airborne = true; _jumpId = action.jumpId; break;
    case MovementActionKind::Land:
        if (!_airborne || action.jumpId != _jumpId || !action.grounded) return false;
        _airborne = false; break;
    case MovementActionKind::Respawn:
        if (action.jumpId != _jumpId || !action.grounded) return false;
        _airborne = false; break;
    default:
        if (action.jumpId != _jumpId || (action.grounded != 0) == _airborne) return false;
        break;
    }
    _actionTick = action.clientTick; _acceptedActionSequence = action.sequence;
    _state3D = action;
    SetPosition(static_cast<int32_t>(std::llround(action.x / 1000.0)), static_cast<int32_t>(std::llround(action.y / 1000.0)));
    return true;
}

bool Player::AcceptActionBatch(uint64_t generation, uint64_t sequence, uint64_t latestTick)
{
    if (generation != _generation || sequence <= _batchSequence || latestTick < _latestClientTick)
        return false;
    _batchSequence = sequence;
    _latestClientTick = latestTick;
    return true;
}

bool Player::AcceptAction(const MovementAction& action)
{
    if (action.sequence <= _actionSequence || action.clientTick < _actionTick ||
        action.horizontal < -1 || action.horizontal > 1 || action.grounded > 1 ||
        action.kind > MovementActionKind::Fall)
        return false;
    // 거절한 점프도 재전송으로 나중에 살아나지 않게 요청 번호를 소비한다.
    _actionSequence = action.sequence;
    switch (action.kind)
    {
    case MovementActionKind::Jump:
    case MovementActionKind::Fall:
        if (_airborne || !_platformState.grounded || action.grounded != 0 || action.jumpId <= _jumpId)
            return false;
        _airborne = true;
        _jumpId = action.jumpId;
        break;
    case MovementActionKind::Land:
        if (!_airborne || action.jumpId != _jumpId || action.grounded == 0)
            return false;
        _airborne = false;
        break;
    case MovementActionKind::Respawn:
        if (action.jumpId != _jumpId || action.grounded == 0)
            return false;
        _airborne = false;
        break;
    default:
        // 위치 확인이나 입력 변경으로 착지 잠금을 해제할 수 없다.
        if (action.jumpId != _jumpId || (action.grounded != 0) == _airborne)
            return false;
        break;
    }
    _actionTick = action.clientTick;
    _acceptedActionSequence = action.sequence;
    _platformState.x = action.x / 1000.0;
    _platformState.y = action.y / 1000.0;
    _platformState.velocityX = action.velocityX / 1000.0;
    _platformState.velocityY = action.velocityY / 1000.0;
    _platformState.footholdId = action.footholdId;
    _platformState.grounded = action.grounded != 0;
    _platformState.jumpHeld = false;
    SetPosition(static_cast<int32_t>(std::llround(_platformState.x)), static_cast<int32_t>(std::llround(_platformState.y)));
    return true;
}
