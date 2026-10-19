#include "InventoryRepository.h"
#include "TradeRules.h"
#include <mysql/jdbc.h>
#include <cstring>
#include <iostream>

TradeCommitResult InventoryRepository::Exchange(const TradeCommitRequest& request)
{
    TradeCommitResult result;
    if (!request.tradeId || request.characters[0] == request.characters[1]) { result.result=TradeResult::Invalid; return result; }
    struct Guard
    {
        sql::Connection& connection; bool committed=false;
        ~Guard() { try { if(!committed) connection.rollback(); connection.setAutoCommit(true); } catch (...) {} }
    };
    try
    {
        if (!TryReconnect()) return result;
        _connection.setAutoCommit(false); Guard guard{_connection};
        // 연결이 늘어나도 두 캐릭터의 행을 같은 순서로 잠근다.
        int first = request.characters[0] < request.characters[1] ? 0 : 1;
        for (int index : {first,1-first})
        {
            std::unique_ptr<sql::PreparedStatement> owner(_connection.prepareStatement("SELECT id FROM characters WHERE id=? AND account_id=? FOR UPDATE"));
            owner->setUInt(1,request.characters[index]); owner->setUInt(2,request.accounts[index]);
            std::unique_ptr<sql::ResultSet> row(owner->executeQuery());
            if (!row->next() || !Read(request.characters[index],result.inventories[index])) { result.result=TradeResult::Invalid; return result; }
        }
        std::string payload(reinterpret_cast<const char*>(&request),sizeof(request));
        std::unique_ptr<sql::PreparedStatement> recorded(_connection.prepareStatement("SELECT request_payload FROM character_trade_records WHERE trade_id=?"));
        recorded->setUInt64(1,request.tradeId); std::unique_ptr<sql::ResultSet> existing(recorded->executeQuery());
        // 같은 거래 번호와 요청 내용이 이미 저장됐으면 교환을 반복하지 않고 성공으로 처리한다.
        if (existing->next())
        {
            result.result = existing->getString(1).asStdString() == payload ? TradeResult::Success : TradeResult::Invalid;
            return result;
        }
        InventorySnapshotPacket original[2]{result.inventories[0],result.inventories[1]};
        result.result=ApplyTrade(request,result.inventories); if(result.result!=TradeResult::Success) return result;
        // 장비의 복합 외래 키를 유지하기 위해 기존 슬롯을 먼저 비우고 소유권을 옮긴다.
        for (int side=0;side<2;++side) for(uint8_t i=0;i<request.offers[side].count;++i)
        {
            const auto& item=request.offers[side].items[i]; if(!item.instanceId) continue;
            std::unique_ptr<sql::PreparedStatement> remove(_connection.prepareStatement("DELETE FROM character_inventory_slots WHERE character_id=? AND instance_id=?"));
            remove->setUInt(1,request.characters[side]); remove->setUInt64(2,item.instanceId);
            if(remove->executeUpdate()!=1) throw std::runtime_error("Trade source equipment missing");
            std::unique_ptr<sql::PreparedStatement> transfer(_connection.prepareStatement("UPDATE character_item_instances SET character_id=?, origin_slot=NULL WHERE instance_id=? AND character_id=? AND item_id=?"));
            transfer->setUInt(1,request.characters[1-side]); transfer->setUInt64(2,item.instanceId); transfer->setUInt(3,request.characters[side]); transfer->setUInt(4,item.itemId);
            if(transfer->executeUpdate()!=1) throw std::runtime_error("Trade equipment owner mismatch");
        }
        for(int side=0;side<2;++side) Write(request.characters[side],original[side],result.inventories[side],false);
        // 양쪽 인벤토리와 완료 기록을 함께 커밋해 재시도 시 교환 여부를 확인할 수 있도록 한다.
        std::unique_ptr<sql::PreparedStatement> receipt(_connection.prepareStatement("INSERT INTO character_trade_records(trade_id,first_character_id,second_character_id,request_payload) VALUES (?,?,?,?)"));
        receipt->setUInt64(1,request.tradeId); receipt->setUInt(2,request.characters[0]); receipt->setUInt(3,request.characters[1]); receipt->setString(4,payload); receipt->executeUpdate();
        _connection.commit(); guard.committed=true;
        if(_afterCommit) _afterCommit();
        return result;
    }
    catch(const sql::SQLException& error) { ConnectionFailed(error.getErrorCode(),error.getSQLState()); std::cerr<<"Trade transaction failed: "<<error.what()<<'\n'; }
    catch(const std::exception& error) { std::cerr<<"Trade transaction failed: "<<error.what()<<'\n'; }
    result.result=TradeResult::StorageError; return result;
}

TradeLookupResult InventoryRepository::LookupTrade(uint32_t account,uint32_t character,uint64_t tradeId)
{
    TradeLookupResult result; result.state.tradeId=tradeId; result.state.characters[0]=character; result.state.result=TradeResult::StorageError; result.state.stage=TradeStage::Committing;
    try
    {
        if(!TryReconnect()) return result;
        _connection.setAutoCommit(false);
        struct ReadGuard { sql::Connection& connection; ~ReadGuard() { try { connection.rollback(); connection.setAutoCommit(true); } catch(...) {} } } guard{_connection};
        // 교환과 같은 캐릭터 행 잠금을 사용해 진행 중인 커밋이 끝난 뒤 결과와 인벤토리를 읽는다.
        std::unique_ptr<sql::PreparedStatement> owner(_connection.prepareStatement("SELECT id FROM characters WHERE id=? AND account_id=? FOR UPDATE"));
        owner->setUInt(1,character); owner->setUInt(2,account); std::unique_ptr<sql::ResultSet> owned(owner->executeQuery());
        if(!owned->next()) { result.state.stage=TradeStage::Cancelled; result.state.result=TradeResult::Invalid; return result; }
        std::unique_ptr<sql::PreparedStatement> lookup(_connection.prepareStatement("SELECT request_payload FROM character_trade_records WHERE trade_id=? AND (first_character_id=? OR second_character_id=?)"));
        lookup->setUInt64(1,tradeId); lookup->setUInt(2,character); lookup->setUInt(3,character); std::unique_ptr<sql::ResultSet> row(lookup->executeQuery());
        if(row->next())
        {
            auto bytes=row->getString(1).asStdString(); if(bytes.size()!=sizeof(TradeCommitRequest)) return result;
            TradeCommitRequest request; memcpy(&request,bytes.data(),sizeof(request));
            for(int side=0;side<2;++side) { result.state.characters[side]=request.characters[side]; result.state.offers[side]=request.offers[side]; result.state.confirmed[side]=1; }
            result.state.stage=TradeStage::Completed; result.state.result=TradeResult::Success;
        }
        else { result.state.stage=TradeStage::Cancelled; result.state.result=TradeResult::Expired; }
        result.loaded=Read(character,result.inventory);
        return result;
    }
    catch(const sql::SQLException& error) { ConnectionFailed(error.getErrorCode(),error.getSQLState()); }
    catch(const std::exception&) {}
    result.state.stage=TradeStage::Committing; result.state.result=TradeResult::StorageError; result.loaded=false; return result;
}
