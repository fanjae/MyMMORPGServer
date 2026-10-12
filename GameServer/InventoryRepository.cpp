#include "InventoryRepository.h"
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
InventoryRepository::InventoryRepository(sql::Connection& connection) : _connection(connection), _schema(connection.getSchema()) {}

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
        "SELECT category, slot, item_id, quantity FROM character_inventory_slots WHERE character_id = ? ORDER BY category, slot"));
    slots->setUInt(1, id); std::unique_ptr<sql::ResultSet> entries(slots->executeQuery());
    while (entries->next())
    {
        uint32_t category = entries->getUInt("category"), slot = entries->getUInt("slot"), quantity = entries->getUInt("quantity");
        if (category >= 5 || slot >= 24 || quantity == 0 || quantity > UINT16_MAX) throw std::runtime_error("Invalid inventory slot in database");
        auto& entry = state.entries[category * 24 + slot];
        if (entry.itemId != 0) throw std::runtime_error("Duplicate inventory slot in database");
        entry = {entries->getUInt("item_id"), static_cast<uint16_t>(quantity)};
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
    for (uint32_t i = 0; i < INVENTORY_CATEGORIES * INVENTORY_SLOTS; ++i)
    {
        const auto& entry = state.entries[i];
        const auto& previous = original.entries[i];
        if (create ? !entry.itemId : (entry.itemId == previous.itemId && entry.quantity == previous.quantity)) continue;
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
                "INSERT INTO character_inventory_slots (character_id, category, slot, item_id, quantity) VALUES (?, ?, ?, ?, ?) "
                "ON DUPLICATE KEY UPDATE item_id = ?, quantity = ?"));
            insert->setUInt(1, id); insert->setUInt(2, i / INVENTORY_SLOTS); insert->setUInt(3, i % INVENTORY_SLOTS);
            insert->setUInt(4, entry.itemId); insert->setUInt(5, entry.quantity); insert->setUInt(6, entry.itemId); insert->setUInt(7, entry.quantity);
            insert->executeUpdate();
        }
        ++_writes.slots;
    }
}
InventorySnapshotPacket InventoryRepository::Apply(uint32_t accountId, uint32_t id, const InventoryRequestPacket& request,
    const std::shared_ptr<std::atomic<bool>>& canceled, std::chrono::steady_clock::time_point deadline)
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
        if (!exists) original = InventoryService::Starter();
        auto result = InventoryService::Apply(original, request);
        if (expired()) return failure;
        if (!exists || (result.result == InventoryResult::Success && result.revision != original.revision))
            Write(id, original, result.result == InventoryResult::Success ? result : original, !exists);
        if (expired()) return failure;
        // 커밋 직전까지 확인한 취소·시간 초과 요청은 위의 조기 반환으로 롤백한다.
        transaction.Commit(); return result;
    }
    catch (const sql::SQLException& error)
    {
        ConnectionFailed(error.getErrorCode(), error.getSQLState());
        std::cerr << "Inventory database transaction failed: " << error.what() << '\n'; return failure;
    }
    catch (const std::exception& error)
    { std::cerr << "Inventory database transaction failed: " << error.what() << '\n'; return failure; }
}
