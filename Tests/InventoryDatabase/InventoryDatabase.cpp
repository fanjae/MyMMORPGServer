#include "../../ServerCore/DatabaseConnection.h"
#include "../../GameServer/InventoryRepository.h"
#include "../../GameServer/CharacterRepository.h"
#include <iostream>
#include <memory>
#include <future>
#include <thread>
#include <stdexcept>
#include <cstdlib>
#include <cstring>

namespace
{
    void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
    std::string Environment(const char* name)
    {
        char* value = nullptr; size_t length = 0;
        _dupenv_s(&value, &length, name); std::string result = value ? value : ""; free(value); return result;
    }
    void Connect(DatabaseConnection& connection)
    {
        // 기존 게임 데이터를 수정하지 않도록 격리된 검사 DB 이름만 허용한다.
        Check(Environment("DB_NAME").find("mymmorpg_full_test") == 0, "Use the isolated integration database only");
        Check(connection.Connect(Environment("DB_HOST"), Environment("DB_USER"), Environment("DB_PASSWORD"), Environment("DB_NAME")), "Test database connection failed");
    }
    InventorySnapshotPacket Apply(InventoryRepository& repository, uint32_t account, uint32_t character, InventoryRequestPacket request = {})
    { return repository.Apply(account, character, request, std::make_shared<std::atomic<bool>>(false), std::chrono::steady_clock::now() + std::chrono::seconds(5)); }
    uint64_t Scalar(sql::Connection& connection, const char* query)
    {
        std::unique_ptr<sql::Statement> statement(connection.createStatement());
        std::unique_ptr<sql::ResultSet> row(statement->executeQuery(query)); Check(row->next(), "Scalar query returned no rows"); return row->getUInt64(1);
    }
    void Execute(sql::Connection& connection, const char* query)
    { std::unique_ptr<sql::Statement> statement(connection.createStatement()); statement->execute(query); }
}
int main()
{
    try
    {
        DatabaseConnection database; Connect(database); auto& connection = *database.GetConnection(); InventoryRepository repository(connection);
        Check(Scalar(connection, "SELECT COUNT(*) FROM character_inventories") == 0, "Tests require a fresh isolated inventory database");
        Check(Apply(repository, 2, 1001).result == InventoryResult::StorageError && Scalar(connection, "SELECT COUNT(*) FROM character_inventories") == 0,
            "Wrong account created another character inventory");
        auto original = Apply(repository, 1, 1001);
        Check(original.result == InventoryResult::Success && original.revision == 1 && original.entries[24].quantity == 10 &&
            Scalar(connection, "SELECT COUNT(*) FROM character_inventory_slots WHERE character_id=1001") == 9, "Starter rows were not committed");
        std::cout << "[PASS] DB owner check and transactional first-time inventory creation\n";

        auto merged = Apply(repository, 1, 1001, {InventoryOperation::Move, 1, 4, 0, original.revision});
        Check(repository.LastWriteMetrics().headers == 1 && repository.LastWriteMetrics().slots == 2, "Merge rewrote unchanged DB slots");
        auto used = Apply(repository, 1, 1001, {InventoryOperation::Use, 1, 0, 0, merged.revision});
        Check(repository.LastWriteMetrics().headers == 1 && repository.LastWriteMetrics().slots == 1, "Potion use rewrote unchanged DB slots");
        auto sorted = Apply(repository, 1, 1001, {InventoryOperation::Sort, 1, 0, 0, used.revision});
        Check(sorted.revision == used.revision && repository.LastWriteMetrics().headers == 0 && repository.LastWriteMetrics().slots == 0, "No-op sort wrote to DB");
        auto stale = Apply(repository, 1, 1001, {InventoryOperation::Use, 1, 0, 0, original.revision});
        Check(merged.entries[24].quantity == 15 && merged.entries[28].itemId == 0 && used.hp == 80 && used.entries[24].quantity == 14 &&
            stale.result == InventoryResult::StaleRevision && stale.revision == used.revision, "DB mutations or duplicate request protection failed");
        Check(Apply(repository, 1, 1002).entries[24].quantity == 10 && Apply(repository, 2, 2001).entries[24].quantity == 10, "DB character inventories leaked");
        DatabaseConnection restarted; Connect(restarted); InventoryRepository fresh(*restarted.GetConnection()); auto loaded = Apply(fresh, 1, 1001);
        Check(loaded.revision == used.revision && loaded.hp == 80 && loaded.entries[24].quantity == 14, "Inventory lost across connection/repository recreation");
        std::cout << "[PASS] DB merge/use, stale revision, character isolation and reconnect persistence\n";

        // 별도 Connection에서 같은 변경 번호의 요청을 보내 물약이 한 번만 소비되는지 확인한다.
        DatabaseConnection concurrentA, concurrentB; Connect(concurrentA); Connect(concurrentB);
        auto cancel = std::make_shared<std::atomic<bool>>(false); auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        InventoryRequestPacket consume{InventoryOperation::Use, 1, 0, 0, loaded.revision};
        InventorySnapshotPacket resultA, resultB;
        auto run = [&](DatabaseConnection& db, InventorySnapshotPacket& result)
        {
            auto driver = sql::mysql::get_driver_instance(); driver->threadInit();
            InventoryRepository competing(*db.GetConnection()); result = competing.Apply(1, 1001, consume, cancel, deadline); driver->threadEnd();
        };
        std::thread first([&] { run(concurrentA, resultA); }), second([&] { run(concurrentB, resultB); }); first.join(); second.join();
        Check((resultA.result == InventoryResult::Success && resultB.result == InventoryResult::StaleRevision) ||
            (resultB.result == InventoryResult::Success && resultA.result == InventoryResult::StaleRevision), "Concurrent consumers both committed");
        auto beforeFailure = Apply(repository, 1, 1001);
        Check(beforeFailure.hp == 100 && beforeFailure.entries[24].quantity == 13 && beforeFailure.revision == loaded.revision + 1, "Concurrent inventory quantity incorrect");
        std::cout << "[PASS] Independent DB connections serialize duplicate consumption using row locks\n";

        // 헤더 변경 뒤 슬롯 저장을 강제로 실패시켜 수량과 회복 효과가 함께 롤백되는지 확인한다.
        Execute(connection, "ALTER TABLE character_inventory_slots ADD CONSTRAINT ck_inventory_test_failure CHECK (NOT (character_id=1001 AND category=1 AND slot=1 AND quantity=7))");
        auto rejected = Apply(repository, 1, 1001, {InventoryOperation::Use, 1, 1, 0, beforeFailure.revision});
        Execute(connection, "ALTER TABLE character_inventory_slots DROP CHECK ck_inventory_test_failure");
        auto afterFailure = Apply(repository, 1, 1001);
        Check(rejected.result == InventoryResult::StorageError && beforeFailure.revision == afterFailure.revision && beforeFailure.mp == afterFailure.mp &&
            memcmp(beforeFailure.entries, afterFailure.entries, sizeof(beforeFailure.entries)) == 0, "Failed slot write left partially updated inventory or MP");
        std::cout << "[PASS] Forced SQL failure rolls back header, slots and potion effect together\n";

        // DB 제약 범위 안이지만 아이템 중첩 한도를 넘는 상태가 덮어써지지 않는지 확인한다.
        Execute(connection, "UPDATE character_inventory_slots SET quantity=101 WHERE character_id=1001 AND category=1 AND slot=0");
        Check(Apply(repository, 1, 1001).result == InventoryResult::StorageError &&
            Scalar(connection, "SELECT quantity FROM character_inventory_slots WHERE character_id=1001 AND category=1 AND slot=0") == 101, "Corrupt DB state replaced with starters");
        Execute(connection, "UPDATE character_inventory_slots SET quantity=13 WHERE character_id=1001 AND category=1 AND slot=0");
        std::cout << "[PASS] Invalid persisted quantities are rejected without overwriting stored state\n";

        // 행 잠금으로 첫 작업을 대기시킨 뒤 다음 작업을 취소해 비동기 처리와 취소를 확인한다.
        DatabaseConnection lockDatabase, workerDatabase; Connect(lockDatabase); Connect(workerDatabase);
        auto& locking = *lockDatabase.GetConnection(); locking.setAutoCommit(false);
        Scalar(locking, "SELECT id FROM characters WHERE id=1001 FOR UPDATE");
        {
            CharacterRepository worker(*workerDatabase.GetConnection());
            auto canceled = std::make_shared<std::atomic<bool>>(false);
            auto started = std::chrono::steady_clock::now();
            auto waiting = worker.ApplyInventoryAsync(1, 1001, {}, std::make_shared<std::atomic<bool>>(false));
            Check(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(100) && waiting.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout,
                "Inventory request blocked the caller on DB row lock");
            auto queued = worker.ApplyInventoryAsync(1, 1002, {InventoryOperation::Use, 1, 0, 0, 1}, canceled); canceled->store(true);
            locking.rollback(); locking.setAutoCommit(true);
            Check(waiting.get().result == InventoryResult::Success && queued.get().result == InventoryResult::StorageError &&
                Apply(repository, 1, 1002).entries[24].quantity == 10, "Canceled queued inventory transaction committed");
        }
        std::cout << "[PASS] DB worker submission stays nonblocking and canceled queued mutations do not commit\n";
        // 별도 연결에서 작업 Connection을 끊어도 변경 요청을 재실행하지 않고 다음 조회로 복구한다.
        uint64_t connectionId = Scalar(connection, "SELECT CONNECTION_ID()");
        DatabaseConnection killer; Connect(killer);
        std::string kill = "KILL CONNECTION " + std::to_string(connectionId); Execute(*killer.GetConnection(), kill.c_str());
        auto lost = Apply(repository, 1, 1002, {InventoryOperation::Use, 1, 0, 0, 1});
        Check(lost.result == InventoryResult::StorageError, "Disconnected operation reported success");
        auto recovered = Apply(repository, 1, 1002);
        Check(recovered.result == InventoryResult::Success && recovered.revision == 1 && recovered.entries[24].quantity == 10,
            "Reconnect lost stored state or replayed failed consumption");
        std::cout << "[PASS] Lost DB connection recovers on refresh without replaying a mutation\n";
        std::cout << "[PASS] Incremental DB writes: merge 2 slots, potion 1 slot, no-op sort 0 rows\n";
        // 최초 오류가 캐릭터 조회에서 발생해도 같은 작업 큐의 다음 입장 조회가 복구되어야 한다.
        DatabaseConnection entryDatabase; Connect(entryDatabase);
        uint64_t entryId = Scalar(*entryDatabase.GetConnection(), "SELECT CONNECTION_ID()");
        CharacterRepository entryWorker(*entryDatabase.GetConnection());
        kill = "KILL CONNECTION " + std::to_string(entryId); Execute(*killer.GetConnection(), kill.c_str());
        auto failedEntry = entryWorker.FindByIdAsync(1, 1001, std::make_shared<std::atomic<bool>>(false)).get();
        auto recoveredEntry = entryWorker.FindByIdAsync(1, 1001, std::make_shared<std::atomic<bool>>(false)).get();
        Check(failedEntry.status == CharacterLoadStatus::DatabaseError && recoveredEntry.status == CharacterLoadStatus::Success &&
            recoveredEntry.character.characterId == 1001, "Shared DB worker did not recover character lookup");
        std::cout << "[PASS] Character lookup recovers the shared DB connection after an initial disconnect\n";
        // 장비의 고유 번호와 착용 위치를 이동·교환·롤백·재조회에서도 유지한다.
        auto gear=Apply(repository,1,1002); uint64_t swordId=gear.entries[0].instanceId;
        auto worn=Apply(repository,1,1002,{InventoryOperation::Equip,0,0,0,gear.revision});
        Check(worn.result==InventoryResult::Success && worn.equipped[0].instanceId==swordId && !worn.entries[0].itemId,"Equip did not move owned instance");
        auto fullGear=Apply(repository,1,1002,{InventoryOperation::Unequip,0,0,1,worn.revision});
        Check(fullGear.result==InventoryResult::Full && fullGear.equipped[0].instanceId==swordId,"Unequip overwrote occupied slot");
        auto removed=Apply(repository,1,1002,{InventoryOperation::Unequip,0,0,0,worn.revision});
        Check(removed.result==InventoryResult::Success && removed.entries[0].instanceId==swordId && !removed.equipped[0].itemId,"Unequip lost instance");
        auto swapped=Apply(repository,1,1002,{InventoryOperation::Move,0,0,1,removed.revision});
        Check(swapped.entries[1].instanceId==swordId,"Equipment swap violated unique instance locations");
        auto restoredGear=Apply(repository,1,1002,{InventoryOperation::Move,0,0,1,swapped.revision});
        Execute(connection,"ALTER TABLE character_equipment ADD CONSTRAINT ck_equipment_test_failure CHECK (slot<>0)");
        auto gearFailure=Apply(repository,1,1002,{InventoryOperation::Equip,0,0,0,restoredGear.revision});
        Execute(connection,"ALTER TABLE character_equipment DROP CHECK ck_equipment_test_failure");
        auto stableGear=Apply(repository,1,1002);
        Check(gearFailure.result==InventoryResult::StorageError && stableGear.revision==restoredGear.revision && stableGear.entries[0].instanceId==swordId,"Equipment failure left a lost slot");
        std::cout << "[PASS] DB equipment instance identity, empty-slot validation, swap and atomic rollback\n";
        auto internal=[&](InventoryMutation mutation,uint32_t account=1,uint32_t character=1002)
        { return repository.Apply(account,character,{},std::make_shared<std::atomic<bool>>(false),std::chrono::steady_clock::now()+std::chrono::seconds(5),mutation); };
        InventoryMutation damage; damage.damage=10; auto hurt=internal(damage);
        auto healed=Apply(repository,1,1002,{InventoryOperation::Use,1,0,0,hurt.revision});
        Check(hurt.hp==40 && healed.hp==70 && healed.entries[24].quantity==9,"Damage and potion order lost HP or quantity");
        damage.damage=100; auto dead=internal(damage);
        Check(dead.hp==0 && Apply(repository,1,1002,{InventoryOperation::Use,1,0,0,dead.revision}).result==InventoryResult::CannotUse,"Potion bypassed death");
        InventoryMutation revive; revive.revive=true; Check(internal(revive).hp==100,"Revive did not persist HP");
        std::cout << "[PASS] DB serialized damage/potion/death/revive without full slot writes\n";
        InventoryMutation grant; grant.dropId=700000001; grant.itemId=101; grant.quantity=1;
        InventorySnapshotPacket claimA,claimB;
        auto claim=[&](DatabaseConnection& db,uint32_t account,uint32_t character,InventorySnapshotPacket& reply)
        {
            auto driver=sql::mysql::get_driver_instance(); driver->threadInit(); InventoryRepository claims(*db.GetConnection());
            reply=claims.Apply(account,character,{},std::make_shared<std::atomic<bool>>(false),std::chrono::steady_clock::now()+std::chrono::seconds(5),grant); driver->threadEnd();
        };
        std::thread firstClaim([&]{claim(concurrentA,1,1002,claimA);}), secondClaim([&]{claim(concurrentB,2,2001,claimB);}); firstClaim.join(); secondClaim.join();
        Check((claimA.result==InventoryResult::Success && claimB.result==InventoryResult::AlreadyTaken) ||
            (claimB.result==InventoryResult::Success && claimA.result==InventoryResult::AlreadyTaken),"Concurrent drop granted twice");
        Check(internal(grant).result==InventoryResult::AlreadyTaken && Scalar(connection,"SELECT COUNT(*) FROM field_drop_claims WHERE drop_id=700000001")==1,"Duplicate drop claim paid twice");
        std::cout << "[PASS] DB concurrent characters and replay receive one transactional drop grant\n";
        Execute(connection,"DELETE FROM character_inventory_slots WHERE character_id=1002 AND category=1");
        for (int i=0;i<24;++i) { auto query="INSERT INTO character_inventory_slots (character_id,category,slot,item_id,quantity) VALUES (1002,1,"+std::to_string(i)+",101,100)"; Execute(connection,query.c_str()); }
        grant.dropId=700000002; Check(internal(grant).result==InventoryResult::Full && Scalar(connection,"SELECT COUNT(*) FROM field_drop_claims WHERE drop_id=700000002")==0,"Full inventory consumed drop claim");
        std::cout << "[PASS] DB full inventory preserves unclaimed drop\n";
        // 커밋 응답을 잃었다고 가정해 같은 피격 번호를 다시 보내도 HP를 두 번 줄이지 않는다.
        InventoryMutation retryDamage; retryDamage.damage = 10; retryDamage.mutationId = 800000001;
        auto once = internal(retryDamage), twice = internal(retryDamage);
        Check(once.hp == twice.hp && once.revision == twice.revision &&
            Scalar(connection, "SELECT COUNT(*) FROM field_combat_mutations WHERE mutation_id=800000001") == 1, "Damage retry applied twice");
        retryDamage.damage = 11;
        Check(internal(retryDamage).result == InventoryResult::StorageError, "Changed retry payload accepted");
        retryDamage.damage = 10; retryDamage.mutationId = 800000002;
        Execute(connection, "ALTER TABLE character_inventories ADD CONSTRAINT ck_damage_retry_failure CHECK (character_id<>1002 OR hp=90)");
        Check(internal(retryDamage).result == InventoryResult::StorageError &&
            Scalar(connection, "SELECT COUNT(*) FROM field_combat_mutations WHERE mutation_id=800000002") == 0, "Failed damage retained its dedupe record");
        Execute(connection, "ALTER TABLE character_inventories DROP CHECK ck_damage_retry_failure");
        auto recoveredDamage = internal(retryDamage);
        Check(recoveredDamage.hp == 80 && internal(retryDamage).hp == 80, "Rolled back damage could not recover exactly once");
        std::cout << "[PASS] DB damage retry deduplicates committed results and atomically rolls back/retries rejected changes\n";
        InventoryMutation lookup; lookup.dropId = 700000001; lookup.queryDrop = true;
        auto claimOwner = Scalar(connection, "SELECT character_id FROM field_drop_claims WHERE drop_id=700000001");
        auto queriedOwner = internal(lookup, claimOwner == 1002 ? 1 : 2, uint32_t(claimOwner));
        auto queriedOther = internal(lookup, claimOwner == 1002 ? 2 : 1, claimOwner == 1002 ? 2001 : 1002);
        lookup.dropId = 799999999; auto missingClaim = internal(lookup);
        Check(queriedOwner.result == InventoryResult::Success && queriedOther.result == InventoryResult::AlreadyTaken &&
            missingClaim.result == InventoryResult::InvalidSlot && Scalar(connection, "SELECT COUNT(*) FROM field_drop_claims WHERE drop_id=700000001") == 1,
            "Pickup result lookup changed grants or misidentified owner");
        Check(internal(retryDamage).revision == recoveredDamage.revision, "Result lookup mutated inventory revision");
        std::cout << "[PASS] DB pickup result queries distinguish owner/other/no record without awarding or consuming items\n";
        Execute(connection, "INSERT INTO field_drop_claims (drop_id,character_id,item_id,quantity,acquired_at) VALUES (799999998,1002,101,1,NOW()-INTERVAL 31 DAY)");
        Execute(connection, "INSERT INTO field_combat_mutations (mutation_id,character_id,damage,applied_at) VALUES (899999999,1002,1,NOW()-INTERVAL 31 DAY)");
        InventoryRepository cleanup(connection); Apply(cleanup, 1, 1002);
        Check(Scalar(connection, "SELECT COUNT(*) FROM field_drop_claims WHERE drop_id=799999998") == 0 &&
            Scalar(connection, "SELECT COUNT(*) FROM field_combat_mutations WHERE mutation_id=899999999") == 0 &&
            Scalar(connection, "SELECT COUNT(*) FROM field_combat_mutations WHERE mutation_id=800000001") == 1,
            "Record cleanup deleted active dedupe records or retained expired ones");
        std::cout << "[PASS] DB record cleanup removes expired history and retains recent claims/damage\n";
        // 뒤의 실제 실행 파일 검사가 같은 최초 상태를 사용할 수 있도록 이번 검사 캐릭터만 복원한다.
        Execute(connection,"DELETE FROM character_inventory_slots WHERE character_id IN (1002,2001) AND category=1");
        for (uint32_t character : {1002u,2001u})
        {
            auto rows="INSERT INTO character_inventory_slots (character_id,category,slot,item_id,quantity) VALUES ("+std::to_string(character)+",1,0,101,10),("+std::to_string(character)+",1,1,102,8),("+std::to_string(character)+",1,4,101,5)"; Execute(connection,rows.c_str());
        }
        Execute(connection,"UPDATE character_inventories SET revision=1,hp=50,mp=25 WHERE character_id IN (1002,2001)");
        std::cout << "PASS 16 inventory database integration groups\n"; return 0;
    }
    catch (const std::exception& error) { std::cerr << "[FAIL] " << error.what() << '\n'; return 1; }
}
