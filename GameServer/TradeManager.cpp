#include "TradeManager.h"
#include "GameSession.h"
#include "Player.h"
#include "PlayerManager.h"
#include "Map.h"
#include "GamePacketHandler.h"
#include "FieldIdentity.h"
#include "../ServerCore/Packet.h"
#include <unordered_map>
#include <future>
#include <chrono>
#include <cstring>

namespace
{
    using Clock=std::chrono::steady_clock;
    struct Trade
    {
        TradeState state; TradeCommitRequest request;
        std::future<TradeCommitResult> pending;
        CharacterRepository* repository = nullptr;
        Clock::time_point expires{}, retry{};
        uint32_t attempts=0;
    };
    std::unordered_map<uint64_t,Trade> trades;
    Trade* Active(uint32_t character)
    {
        for(auto& entry:trades) if(entry.second.state.stage<TradeStage::Completed)
            for(auto id:entry.second.state.characters) if(id==character) return &entry.second;
        return nullptr;
    }
    bool Send(GameSession& session,const TradeState& state)
    {
        PacketHeader header{uint16_t(sizeof(PacketHeader)+sizeof(state)),uint16_t(GamePacketOpcode::TradeState3D)};
        char bytes[sizeof(header)+sizeof(state)]; memcpy(bytes,&header,sizeof(header)); memcpy(bytes+sizeof(header),&state,sizeof(state)); return session.Send(bytes,sizeof(bytes));
    }
    void Publish(Trade& trade,PlayerManager& players)
    {
        for(auto id:trade.state.characters) if(auto* player=players.FindByCharacterId(id)) if(auto* session=dynamic_cast<GameSession*>(player->GetSession())) Send(*session,trade.state);
    }
    bool Together(Trade& trade,PlayerManager& players)
    {
        auto* a=players.FindByCharacterId(trade.state.characters[0]); auto* b=players.FindByCharacterId(trade.state.characters[1]);
        return a && b && a->GetMap()==b->GetMap() && a->CanAttack() && b->CanAttack() && a->GetState3D().grounded && b->GetState3D().grounded;
    }
    void End(Trade& trade,TradeResult result,PlayerManager& players)
    {
        trade.state.stage=TradeStage::Cancelled; trade.state.result=result; ++trade.state.revision;
        trade.expires=Clock::now()+std::chrono::minutes(2); Publish(trade,players);
    }
}
bool TradeManager::Locked(uint32_t character)
{
    auto* trade=Active(character); return trade && trade->state.stage!=TradeStage::Invited;
}
bool TradeManager::SendState(GameSession& session,const TradeState& state) { return Send(session,state); }
void TradeManager::Cancel(uint32_t character,PlayerManager& players)
{
    if(auto* trade=Active(character)) if(trade->state.stage!=TradeStage::Committing) End(*trade,TradeResult::Unavailable,players);
}
bool TradeManager::Handle(GameSession& session,const TradeRequest& request)
{
    auto& players=session.GetPlayerManager(); auto* player=session.GetPlayer(); if(!player) return false;
    auto error=[&](TradeResult result) { TradeState state; state.result=result; state.characters[0]=player->GetCharacterId(); return Send(session,state); };
    if(request.operation>TradeOperation::Query || request.count>MAX_TRADE_ITEMS) return error(TradeResult::Invalid);
    if(request.operation==TradeOperation::Invite)
    {
        auto* target=players.FindByCharacterId(request.targetId);
        auto* other=target ? dynamic_cast<GameSession*>(target->GetSession()) : nullptr;
        if(!target || !other || target==player || target->GetMap()!=player->GetMap() || !player->GetMap()->Is3D() || !player->CanAttack() || !target->CanAttack() || !player->GetState3D().grounded || !target->GetState3D().grounded) return error(TradeResult::Unavailable);
        if(Active(player->GetCharacterId()) || Active(target->GetCharacterId()) || session.FieldBusy() || other->FieldBusy() || trades.size()>=1024) return error(TradeResult::Busy);
        Trade trade; trade.state.tradeId=NewFieldOperationId(); trade.state.stage=TradeStage::Invited; trade.state.result=TradeResult::Success;
        trade.state.characters[0]=player->GetCharacterId(); trade.state.characters[1]=target->GetCharacterId();
        trade.request.tradeId=trade.state.tradeId;
        trade.request.accounts[0]=session.GetAccountId(); trade.request.accounts[1]=other->GetAccountId();
        for(int side=0;side<2;++side) trade.request.characters[side]=trade.state.characters[side];
        trade.expires=Clock::now()+std::chrono::seconds(30);
        auto& stored=trades.emplace(trade.state.tradeId,std::move(trade)).first->second; Publish(stored,players); return true;
    }
    if(request.operation==TradeOperation::Query)
    {
        // 이전 거래 조회가 새 거래의 표시·잠금을 덮지 않도록 현재 진행 상태를 우선한다.
        if(auto* current=Active(player->GetCharacterId())) if(current->state.tradeId!=request.tradeId) return Send(session,current->state);
        if(!request.tradeId) return error(TradeResult::Expired);
    }
    auto found=trades.find(request.tradeId);
    if(found==trades.end())
    {
        if(request.operation==TradeOperation::Query && !Active(player->GetCharacterId())) return session.BeginTradeLookup(request.tradeId);
        return error(TradeResult::Expired);
    }
    auto& trade=found->second; int side=trade.state.characters[0]==player->GetCharacterId() ? 0 : trade.state.characters[1]==player->GetCharacterId() ? 1 : -1;
    if(side<0) return error(TradeResult::Invalid);
    if(request.operation==TradeOperation::Query)
    {
        // 완료 응답과 인벤토리가 함께 유실됐을 수 있으므로 현재 저장 상태도 다시 전달한다.
        if(trade.state.stage==TradeStage::Completed) return session.BeginTradeLookup(request.tradeId);
        return Send(session,trade.state);
    }
    if(trade.state.stage>=TradeStage::Committing) return Send(session,trade.state);
    if(request.operation==TradeOperation::Cancel) { End(trade,TradeResult::Success,players); return true; }
    if(request.revision!=trade.state.revision) { auto state=trade.state; state.result=TradeResult::Stale; return Send(session,state); }
    if(!Together(trade,players)) { End(trade,TradeResult::Unavailable,players); return true; }
    if(request.operation==TradeOperation::Accept)
    {
        if(side!=1 || trade.state.stage!=TradeStage::Invited) return error(TradeResult::Invalid);
        for(int i=0;i<2;++i)
        {
            auto* member=dynamic_cast<GameSession*>(players.FindByCharacterId(trade.state.characters[i])->GetSession());
            if(member->FieldBusy()) return error(TradeResult::Busy);
            trade.state.offers[i].inventoryRevision=member->InventoryError(InventoryResult::Success).revision;
        }
        trade.state.stage=TradeStage::Editing;
    }
    else if(trade.state.stage!=TradeStage::Editing) return error(TradeResult::Invalid);
    else if(request.operation==TradeOperation::Offer)
    {
        auto inventory=session.InventoryError(InventoryResult::Success); TradeOffer offer;
        if(request.inventoryRevision!=inventory.revision) { auto state=trade.state; state.result=TradeResult::Stale; return Send(session,state); }
        if(request.mesos>inventory.mesos) return error(TradeResult::Invalid);
        offer.inventoryRevision=inventory.revision; offer.mesos=request.mesos; offer.count=request.count;
        bool used[120]{};
        for(uint8_t i=0;i<offer.count;++i)
        {
            auto slot=request.slots[i]; if(slot.category>=5 || slot.slot>=24 || !slot.quantity || used[slot.category*24+slot.slot]) return error(TradeResult::Invalid);
            used[slot.category*24+slot.slot]=true; auto item=inventory.entries[slot.category*24+slot.slot];
            if(!item.itemId || slot.quantity>item.quantity) return error(TradeResult::Stale);
            offer.items[i]={slot,item.itemId,item.instanceId};
        }
        trade.state.offers[side]=offer; trade.state.confirmed[0]=trade.state.confirmed[1]=0;
    }
    else if(request.operation==TradeOperation::Confirm)
    {
        trade.state.confirmed[side]=1;
        if(trade.state.confirmed[0] && trade.state.confirmed[1])
        {
            for(int i=0;i<2;++i)
            {
                auto* member=dynamic_cast<GameSession*>(players.FindByCharacterId(trade.state.characters[i])->GetSession());
                if(member->FieldBusy()) { trade.state.confirmed[side]=0; return error(TradeResult::Busy); }
                trade.request.offers[i]=trade.state.offers[i];
            }
            bool empty=true; for(auto& offer:trade.request.offers) if(offer.count || offer.mesos) empty=false;
            if(empty) { trade.state.confirmed[side]=0; return error(TradeResult::Invalid); }
            trade.state.stage=TradeStage::Committing; trade.repository=&session.GetCharacterRepository(); trade.pending=trade.repository->ExchangeAsync(trade.request);
            for(auto id:trade.state.characters) { auto* member=players.FindByCharacterId(id); member->PauseCombat(true); member->GetMap()->SendVitals(*member); }
        }
    }
    else return error(TradeResult::Invalid);
    ++trade.state.revision; trade.state.result=TradeResult::Success; trade.expires=Clock::now()+std::chrono::minutes(2); Publish(trade,players); return true;
}
void TradeManager::Update(PlayerManager& players)
{
    auto now=Clock::now();
    for(auto it=trades.begin();it!=trades.end();)
    {
        auto& trade=it->second;
        if(trade.state.stage==TradeStage::Committing)
        {
            if(!trade.pending.valid() && now>=trade.retry && trade.repository)
                trade.pending=trade.repository->ExchangeAsync(trade.request);
            if(trade.pending.valid() && trade.pending.wait_for(std::chrono::seconds(0))==std::future_status::ready)
            {
                TradeCommitResult result; try { result=trade.pending.get(); } catch(...) {}
                if(result.result==TradeResult::StorageError)
                {
                    // 커밋 여부가 불명확하면 같은 거래 번호로 재조회하고 새 교환은 허용하지 않는다.
                    trade.state.result=TradeResult::StorageError; trade.retry=now+std::chrono::seconds(1u<<(std::min)(trade.attempts++,3u)); Publish(trade,players);
                }
                else
                {
                    trade.state.stage=result.result==TradeResult::Success ? TradeStage::Completed : TradeStage::Cancelled;
                    trade.state.result=result.result; ++trade.state.revision; trade.expires=now+std::chrono::minutes(2);
                    for(int side=0;side<2;++side) if(auto* player=players.FindByCharacterId(trade.state.characters[side])) if(auto* session=dynamic_cast<GameSession*>(player->GetSession())) session->ApplyTradeInventory(result.inventories[side]);
                    Publish(trade,players);
                }
            }
        }
        else if(trade.state.stage<TradeStage::Completed && now>=trade.expires) End(trade,TradeResult::Expired,players);
        if(trade.state.stage>=TradeStage::Completed && now>=trade.expires) it=trades.erase(it); else ++it;
    }
}
