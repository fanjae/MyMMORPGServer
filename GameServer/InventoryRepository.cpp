#include "InventoryRepository.h"
#include "FieldIdentity.h"
#include "InventoryService.h"
#include <mysql/jdbc.h>
#include <iostream>
#include <stdexcept>

namespace
{
    // 중간 실패나 조기 반환 시 롤백하고 다음 작업을 위해 자동 커밋을 복원한다.
    class Transaction
    {
    public:
        explicit Transaction(sql::Connection& connection) : _connection(connection) { _connection.setAutoCommit(false); }
        ~Transaction()
        {
            try { if (!_committed) _connection.rollback(); _connection.setAutoCommit(true); } catch (const sql::SQLException&) {}
        }
        void Commit() { _connection.commit(); _committed = true; }
    private:
        sql::Connection& _connection; bool _committed = false;
    };
}
InventoryRepository::InventoryRepository(sql::Connection& connection, std::function<void()> afterCommit) :
    _connection(connection), _schema(connection.getSchema()), _afterCommit(std::move(afterCommit)) {}

bool InventoryRepository::TryReconnect()
{
    if (!_reconnect) return true;
    if (!_connection.reconnect()) return false;
    _connection.setSchema(_schema); _connection.setAutoCommit(true); _reconnect = false; return true;
}
void InventoryRepository::ConnectionFailed(int code, const std::string& state)
{
    // 복구 시도 자체가 실패한 경우에도 다음 조회에서 다시 시도할 수 있도록 유지한다.
    _reconnect = _reconnect || state.compare(0, 2, "08") == 0 || code == 2006 || code == 2013;
}

bool InventoryRepository::Read(uint32_t id, InventorySnapshotPacket& state)
{
    // 매 요청마다 DB의 최신 상태를 읽어 다른 서버에서 처리한 변경도 반영한다.
    std::unique_ptr<sql::PreparedStatement> header(_connection.prepareStatement(
        "SELECT revision, mesos, hp, max_hp, mp, max_mp FROM character_inventories WHERE character_id = ? FOR UPDATE"));
    header->setUInt(1, id); std::unique_ptr<sql::ResultSet> row(header->executeQuery());
    if (!row->next()) return false;
    state = {}; state.revision = row->getUInt64("revision"); state.mesos = row->getUInt64("mesos");
    state.hp = static_cast<uint16_t>(row->getUInt("hp")); state.maxHp = static_cast<uint16_t>(row->getUInt("max_hp"));
    state.mp = static_cast<uint16_t>(row->getUInt("mp")); state.maxMp = static_cast<uint16_t>(row->getUInt("max_mp"));
    std::unique_ptr<sql::PreparedStatement> slots(_connection.prepareStatement(
        "SELECT category, slot, item_id, quantity, COALESCE(instance_id,0) AS instance_id FROM character_inventory_slots WHERE character_id = ? ORDER BY category, slot"));
    slots->setUInt(1, id); std::unique_ptr<sql::ResultSet> entries(slots->executeQuery());
    while (entries->next())
    {
        uint32_t category = entries->getUInt("category"), slot = entries->getUInt("slot"), quantity = entries->getUInt("quantity");
        if (category >= 5 || slot >= 24 || quantity == 0 || quantity > UINT16_MAX) throw std::runtime_error("Invalid inventory slot in database");
        auto& entry = state.entries[category * 24 + slot];
        if (entry.itemId != 0) throw std::runtime_error("Duplicate inventory slot in database");
        entry = {entries->getUInt("item_id"), static_cast<uint16_t>(quantity), entries->getUInt64("instance_id")};
    }
    std::unique_ptr<sql::PreparedStatement> equipment(_connection.prepareStatement("SELECT slot,item_id,instance_id FROM character_equipment WHERE character_id=?"));
    equipment->setUInt(1, id); std::unique_ptr<sql::ResultSet> equipped(equipment->executeQuery());
    while (equipped->next())
    {
        auto slot = equipped->getUInt("slot"); if (slot >= 2) throw std::runtime_error("Invalid equipment slot");
        state.equipped[slot] = {equipped->getUInt("item_id"), 1, equipped->getUInt64("instance_id")};
    }
    // 잘못 저장된 상태는 기본 아이템으로 덮어쓰지 않고 저장 오류로 처리한다.
    if (!InventoryService::Validate(state)) throw std::runtime_error("Invalid inventory state in database");
    return true;
}
void InventoryRepository::Write(uint32_t id, const InventorySnapshotPacket& original, const InventorySnapshotPacket& state, bool create)
{
    // 헤더와 실제로 바뀐 슬롯만 같은 트랜잭션에서 저장한다.
    if (create)
    {
        std::unique_ptr<sql::PreparedStatement> insert(_connection.prepareStatement("INSERT INTO character_inventories (character_id) VALUES (?)"));
        insert->setUInt(1, id); insert->executeUpdate();
    }
    std::unique_ptr<sql::PreparedStatement> header(_connection.prepareStatement(
        "UPDATE character_inventories SET revision = ?, mesos = ?, hp = ?, max_hp = ?, mp = ?, max_mp = ? WHERE character_id = ?"));
    header->setUInt64(1, state.revision); header->setUInt64(2, state.mesos); header->setUInt(3, state.hp); header->setUInt(4, state.maxHp);
    header->setUInt(5, state.mp); header->setUInt(6, state.maxMp); header->setUInt(7, id); header->executeUpdate(); ++_writes.headers;
    std::unique_ptr<sql::PreparedStatement> remove, insert;
    // 장비를 교환할 때 기존 위치를 먼저 비워 인스턴스 유일 키가 중간 상태에서 충돌하지 않도록 한다.
    for (uint32_t i = 0; i < 24; ++i)
        if (!create && original.entries[i].itemId && original.entries[i].instanceId != state.entries[i].instanceId)
        {
            std::unique_ptr<sql::PreparedStatement> erase(_connection.prepareStatement("DELETE FROM character_inventory_slots WHERE character_id=? AND category=0 AND slot=?"));
            erase->setUInt(1, id); erase->setUInt(2, i); erase->executeUpdate(); ++_writes.slots;
        }
    for (int i = 0; i < 2; ++i) if (original.equipped[i].instanceId != state.equipped[i].instanceId)
    {
        std::unique_ptr<sql::PreparedStatement> erase(_connection.prepareStatement("DELETE FROM character_equipment WHERE character_id=? AND slot=?"));
        erase->setUInt(1, id); erase->setUInt(2, i); erase->executeUpdate();
    }
    for (uint32_t i = 0; i < INVENTORY_CATEGORIES * INVENTORY_SLOTS; ++i)
    {
        const auto& entry = state.entries[i];
        const auto& previous = original.entries[i];
        if (create ? !entry.itemId : (entry.itemId == previous.itemId && entry.quantity == previous.quantity && entry.instanceId == previous.instanceId)) continue;
        // 사용·이동 요청은 전체 슬롯 삭제 대신 변경된 한두 행만 갱신한다.
        if (!entry.itemId)
        {
            if (!remove) remove.reset(_connection.prepareStatement(
                "DELETE FROM character_inventory_slots WHERE character_id = ? AND category = ? AND slot = ?"));
            remove->setUInt(1, id); remove->setUInt(2, i / INVENTORY_SLOTS); remove->setUInt(3, i % INVENTORY_SLOTS); remove->executeUpdate();
        }
        else
        {
            if (!insert) insert.reset(_connection.prepareStatement(
                "INSERT INTO character_inventory_slots (character_id, category, slot, item_id, quantity, instance_id) VALUES (?, ?, ?, ?, ?, ?) "
                "ON DUPLICATE KEY UPDATE item_id = ?, quantity = ?, instance_id = ?"));
            insert->setUInt(1, id); insert->setUInt(2, i / INVENTORY_SLOTS); insert->setUInt(3, i % INVENTORY_SLOTS);
            insert->setUInt(4, entry.itemId); insert->setUInt(5, entry.quantity);
            if (entry.instanceId) insert->setUInt64(6, entry.instanceId); else insert->setNull(6, sql::DataType::BIGINT);
            insert->setUInt(7, entry.itemId); insert->setUInt(8, entry.quantity);
            if (entry.instanceId) insert->setUInt64(9, entry.instanceId); else insert->setNull(9, sql::DataType::BIGINT);
            insert->executeUpdate();
        }
        ++_writes.slots;
    }
    for (int i = 0; i < 2; ++i) if (state.equipped[i].itemId && original.equipped[i].instanceId != state.equipped[i].instanceId)
    {
        std::unique_ptr<sql::PreparedStatement> equip(_connection.prepareStatement("INSERT INTO character_equipment (character_id,slot,instance_id,item_id) VALUES (?,?,?,?)"));
        equip->setUInt(1,id); equip->setUInt(2,i); equip->setUInt64(3,state.equipped[i].instanceId); equip->setUInt(4,state.equipped[i].itemId); equip->executeUpdate();
    }
}
InventorySnapshotPacket InventoryRepository::Apply(uint32_t accountId, uint32_t id, const InventoryRequestPacket& request,
    const std::shared_ptr<std::atomic<bool>>& canceled, std::chrono::steady_clock::time_point deadline, InventoryMutation mutation)
{
    _writes = {};
    InventorySnapshotPacket failure; failure.result = InventoryResult::StorageError;
    auto expired = [&] { return canceled->load() || std::chrono::steady_clock::now() >= deadline; };
    if (!id || !accountId || expired()) return failure;
    try
    {
        // 연결 오류 뒤의 다음 요청에서만 재연결한다. 결과가 불명확한 사용 요청은 재실행하지 않는다.
        if (!TryReconnect()) return failure;
        Transaction transaction(_connection);
        // 소유 계정을 확인하며 캐릭터 행을 먼저 잠가 최초 생성과 다른 서버의 변경을 직렬화한다.
        std::unique_ptr<sql::PreparedStatement> owner(_connection.prepareStatement("SELECT id FROM characters WHERE id = ? AND account_id = ? FOR UPDATE"));
        owner->setUInt(1, id); owner->setUInt(2, accountId); std::unique_ptr<sql::ResultSet> owned(owner->executeQuery());
        if (!owned->next() || expired()) return failure;
        InventorySnapshotPacket original;
        bool exists = Read(id, original);
        // 조회 결과가 없을 때만 기본 상태를 생성하고 기존 상태에는 규칙을 적용한다.
        if (!exists)
        {
            original = InventoryService::Starter();
            for (int i = 0; i < 2; ++i)
            {
                std::unique_ptr<sql::PreparedStatement> item(_connection.prepareStatement("INSERT INTO character_item_instances (character_id,item_id) VALUES (?,?)"));
                item->setUInt(1,id); item->setUInt(2,original.entries[i].itemId); item->executeUpdate();
                std::unique_ptr<sql::Statement> query(_connection.createStatement()); std::unique_ptr<sql::ResultSet> row(query->executeQuery("SELECT LAST_INSERT_ID()"));
                if (!row->next()) throw std::runtime_error("Equipment instance missing"); original.entries[i].instanceId = row->getUInt64(1);
            }
        }
        // 획득 결과만 조회하고 슬롯·수량·피격은 변경하지 않는다. 소유 캐릭터는 위에서 확인했다.
        if (mutation.queryDrop)
        {
            std::unique_ptr<sql::PreparedStatement> query(_connection.prepareStatement("SELECT character_id FROM field_drop_claims WHERE drop_id=?"));
            query->setUInt64(1, mutation.dropId);
            std::unique_ptr<sql::ResultSet> row(query->executeQuery());
            original.result = !row->next() ? InventoryResult::InvalidSlot : row->getUInt(1) == id ? InventoryResult::Success : InventoryResult::AlreadyTaken;
            return original;
        }
        bool repeatedDamage = false;
        if (mutation.damage)
        {
            if (!mutation.mutationId) mutation.mutationId = NewFieldOperationId();
            std::unique_ptr<sql::PreparedStatement> recorded(_connection.prepareStatement("SELECT character_id,damage FROM field_combat_mutations WHERE mutation_id=?"));
            recorded->setUInt64(1, mutation.mutationId);
            std::unique_ptr<sql::ResultSet> row(recorded->executeQuery());
            repeatedDamage = row->next();
            if (repeatedDamage && (row->getUInt(1) != id || row->getUInt(2) != mutation.damage)) return failure;
            if (!repeatedDamage)
            {
                std::unique_ptr<sql::PreparedStatement> insert(_connection.prepareStatement("INSERT INTO field_combat_mutations (mutation_id,character_id,damage) VALUES (?,?,?)"));
                insert->setUInt64(1, mutation.mutationId); insert->setUInt(2, id); insert->setUInt(3, mutation.damage); insert->executeUpdate();
            }
        }
        bool internal = mutation.damage || mutation.revive || mutation.dropId;
        auto result = repeatedDamage ? original : internal ? InventoryService::Mutate(original, mutation) : InventoryService::Apply(original, request);
        if (mutation.dropId && result.result == InventoryResult::Success)
        {
            // 지급 기록과 슬롯 변경을 같은 트랜잭션에서 확정해 응답 유실·다른 캐릭터 재요청도 중복 지급하지 않는다.
            std::unique_ptr<sql::PreparedStatement> claim(_connection.prepareStatement("INSERT IGNORE INTO field_drop_claims (drop_id,character_id,item_id,quantity) VALUES (?,?,?,?)"));
            claim->setUInt64(1,mutation.dropId); claim->setUInt(2,id); claim->setUInt(3,mutation.itemId); claim->setUInt(4,mutation.quantity);
            if (claim->executeUpdate() != 1) { result = original; result.result = InventoryResult::AlreadyTaken; }
        }
        if (expired()) return failure;
        if (!exists || (result.result == InventoryResult::Success && result.revision != original.revision))
            Write(id, original, result.result == InventoryResult::Success ? result : original, !exists);
        if (expired()) return failure;
        // 커밋 직전까지 확인한 취소·시간 초과 요청은 위의 조기 반환으로 롤백한다.
        transaction.Commit();
        if (_afterCommit) _afterCommit();
        CleanupRecords(); return result;
    }
    catch (const sql::SQLException& error)
    {
        ConnectionFailed(error.getErrorCode(), error.getSQLState());
        std::cerr << "Inventory database transaction failed: " << error.what() << '\n'; return failure;
    }
    catch (const std::exception& error)
    { std::cerr << "Inventory database transaction failed: " << error.what() << '\n'; return failure; }
}

void InventoryRepository::CleanupRecords()
{
    auto now = std::chrono::steady_clock::now();
    if (now < _nextCleanup) return;
    _nextCleanup = now + std::chrono::minutes(5);
    try
    {
        // 필드 드롭은 60초 후 사라지며 기록은 30일 보관한다. 한 번에 지울 행 수도 제한한다.
        std::unique_ptr<sql::Statement> query(_connection.createStatement());
        query->executeUpdate("DELETE FROM field_drop_claims WHERE acquired_at < NOW() - INTERVAL 30 DAY LIMIT 500");
        query->executeUpdate("DELETE FROM field_combat_mutations WHERE applied_at < NOW() - INTERVAL 30 DAY LIMIT 500");
    }
    catch (const sql::SQLException& error)
    {
        ConnectionFailed(error.getErrorCode(), error.getSQLState());
        std::cerr << "Field record cleanup failed: " << error.what() << '\n';
    }
    catch (const std::exception& error)
    {
        // 정리 실패가 이미 커밋된 아이템·HP 변경을 실패 응답으로 바꾸지 않도록 한다.
        std::cerr << "Field record cleanup failed: " << error.what() << '\n';
    }
}
