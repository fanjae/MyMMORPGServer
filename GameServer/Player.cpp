#include "Player.h"

#include <utility>

Player::Player(uint32_t characterId, uint32_t accountId, std::string name, uint16_t level)
    : _characterId(characterId), _accountId(accountId), _name(std::move(name)), _level(level)
{
}