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
        std::cout << "PASS 9 inventory database integration groups\n"; return 0;
    }
    catch (const std::exception& error) { std::cerr << "[FAIL] " << error.what() << '\n'; return 1; }
}
