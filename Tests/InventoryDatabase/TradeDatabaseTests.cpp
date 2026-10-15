#include "../../GameServer/InventoryRepository.h"
#include "../../ServerCore/DatabaseConnection.h"
#include <mysql/jdbc.h>
#include <future>
#include <memory>
#include <iostream>
#include <stdexcept>

void RunTradeDatabaseTests(InventoryRepository& repository,sql::Connection& connection,DatabaseConnection& secondDatabase)
{
    auto check=[](bool ok,const char* message) { if(!ok) throw std::runtime_error(message); };
    auto execute=[&](const char* sql) { std::unique_ptr<sql::Statement> statement(connection.createStatement()); statement->execute(sql); };
    auto read=[&](uint32_t account,uint32_t character) { return repository.Apply(account,character,{},std::make_shared<std::atomic<bool>>(false),std::chrono::steady_clock::now()+std::chrono::seconds(5)); };
    execute("INSERT INTO characters(id,account_id,name) VALUES (9001,1,'TradeStudyA'),(9002,2,'TradeStudyB')");
    auto a=read(1,9001), b=read(2,9002);
    TradeCommitRequest request; request.tradeId=810000001; request.accounts[0]=1; request.accounts[1]=2; request.characters[0]=9001; request.characters[1]=9002;
    request.offers[0].inventoryRevision=a.revision; request.offers[1].inventoryRevision=b.revision;
    request.offers[0].mesos=100; request.offers[1].mesos=50;
    request.offers[0].count=request.offers[1].count=2;
    request.offers[0].items[0]={{1,0,3},101,0}; request.offers[1].items[0]={{1,1,2},102,0};
    request.offers[0].items[1]={{0,0,1},1,a.entries[0].instanceId}; request.offers[1].items[1]={{0,1,1},2,b.entries[1].instanceId};
    auto exchanged=repository.Exchange(request);
    check(exchanged.result==TradeResult::Success,"Atomic trade failed");
    auto aa=read(1,9001), bb=read(2,9002);
    check(aa.mesos==1200 && bb.mesos==1300 && aa.entries[24].quantity==7 && bb.entries[24].quantity==13 && aa.entries[25].quantity==10 && bb.entries[25].quantity==6,"Trade item/mesos conservation failed");
    check(aa.entries[0].instanceId==b.entries[1].instanceId && bb.entries[1].instanceId==a.entries[0].instanceId,"Equipment identity/owner transfer failed");
    check(repository.Exchange(request).result==TradeResult::Success && read(1,9001).revision==aa.revision,"Trade replay changed inventory twice");
    auto changed=request; changed.offers[0].mesos=101; check(repository.Exchange(changed).result==TradeResult::Invalid,"Same trade ID accepted a different offer");
    std::cout<<"[PASS] DB atomic trade conserves items/mesos, transfers equipment owner and deduplicates receipt\n";
    request.tradeId++;
    check(repository.Exchange(request).result==TradeResult::Stale,"Old inventory revision exchanged again");
    execute("DELETE FROM character_inventory_slots WHERE character_id=9002 AND category=1");
    for(int slot=0;slot<24;++slot)
    {
        std::string sql="INSERT INTO character_inventory_slots(character_id,category,slot,item_id,quantity) VALUES (9002,1,"+std::to_string(slot)+",101,100)"; execute(sql.c_str());
    }
    request.offers[0]={}; request.offers[1]={}; request.offers[0].inventoryRevision=aa.revision; request.offers[1].inventoryRevision=bb.revision;
    request.offers[0].count=1; request.offers[0].items[0]={{1,0,1},101,0};
    check(repository.Exchange(request).result==TradeResult::Full && read(1,9001).entries[24].quantity==7,"Full destination partially consumed source");
    std::cout<<"[PASS] DB trade rejects stale state and full destination without partial transfer\n";
    request.tradeId++; request.offers[0].count=0; request.offers[0].mesos=10;
    execute("ALTER TABLE character_trade_records ADD CONSTRAINT ck_trade_test_failure CHECK(trade_id<>810000003)");
    check(repository.Exchange(request).result==TradeResult::StorageError,"Trade SQL failure missing");
    execute("ALTER TABLE character_trade_records DROP CHECK ck_trade_test_failure");
    check(read(1,9001).mesos==1200 && read(2,9002).mesos==1300,"Trade SQL failure did not roll back both players");
    std::cout<<"[PASS] DB trade rolls back both inventories when receipt write fails\n";
    InventoryRepository lostReply(connection,[] { throw std::runtime_error("Trade result lost after commit"); });
    check(lostReply.Exchange(request).result==TradeResult::StorageError,"Post-commit trade response was not lost");
    check(repository.Exchange(request).result==TradeResult::Success && read(1,9001).mesos==1190 && read(2,9002).mesos==1310,"Post-commit retry exchanged twice");
    auto lookup=repository.LookupTrade(1,9001,request.tradeId);
    check(lookup.loaded && lookup.state.stage==TradeStage::Completed && lookup.inventory.mesos==1190,"Persistent trade result query failed");
    check(repository.LookupTrade(2,9001,request.tradeId).state.result==TradeResult::Invalid,"Another account queried trade state");
    std::cout<<"[PASS] DB post-commit trade result loss recovers the same exchange once\n";
    aa=read(1,9001); bb=read(2,9002); request.tradeId++; request.offers[0].inventoryRevision=aa.revision; request.offers[1].inventoryRevision=bb.revision;
    InventoryRepository parallel(*secondDatabase.GetConnection());
    auto task=std::async(std::launch::async,[&] { return parallel.Exchange(request); });
    auto concurrent=repository.Exchange(request);
    check(concurrent.result==TradeResult::Success && task.get().result==TradeResult::Success && read(1,9001).mesos==1180,"Concurrent trade replay duplicated exchange");
    std::cout<<"[PASS] Independent DB connections serialize the same trade receipt\n";
    for(uint32_t character : {9001u,9002u})
    {
        auto current=read(character==9001 ? 1 : 2,character);
        for(int slot=0;slot<24;++slot) if(!current.entries[slot].itemId)
        {
            auto owner=std::to_string(character);
            execute(("INSERT INTO character_item_instances(character_id,item_id) VALUES ("+owner+",1)").c_str());
            execute(("INSERT INTO character_inventory_slots(character_id,category,slot,item_id,quantity,instance_id) VALUES ("+owner+",0,"+std::to_string(slot)+",1,1,LAST_INSERT_ID())").c_str());
        }
    }
    aa=read(1,9001); bb=read(2,9002); request.tradeId++;
    for(int side=0;side<2;++side)
    {
        const auto& current=side==0 ? aa : bb; request.offers[side]={};
        request.offers[side].inventoryRevision=current.revision; request.offers[side].count=1;
        request.offers[side].items[0]={{0,0,1},current.entries[0].itemId,current.entries[0].instanceId};
    }
    auto fullSwap=repository.Exchange(request);
    check(fullSwap.result==TradeResult::Success && read(1,9001).entries[0].instanceId==bb.entries[0].instanceId && read(2,9002).entries[0].instanceId==aa.entries[0].instanceId,"Full equipment inventories could not exchange their freed slots");
    std::cout<<"[PASS] Full equipment inventories exchange atomically using the outgoing slots\n";
    execute("DELETE FROM character_inventory_slots WHERE character_id IN (9001,9002)");
    execute("DELETE FROM character_equipment WHERE character_id IN (9001,9002)");
    execute("DELETE FROM characters WHERE id IN (9001,9002)");
}
