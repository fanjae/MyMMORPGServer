#pragma once
#include "../Protocol/TradePacket.h"
class GameSession;
class PlayerManager;

// 필드와 같은 실행 흐름에서 제안을 관리하고 DB 교환 결과만 비동기로 기다린다.
class TradeManager
{
public:
    static bool Handle(GameSession& session,const TradeRequest& request);
    static bool Locked(uint32_t characterId);
    static void Cancel(uint32_t characterId,PlayerManager& players);
    static void Update(PlayerManager& players);
    static bool SendState(GameSession& session,const TradeState& state);
};
