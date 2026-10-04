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
