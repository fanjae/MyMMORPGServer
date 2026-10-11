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
void InventoryRepository::Write(uint32_t id, const InventorySnapshotPacket& state, bool create)
{
    // 헤더와 슬롯 교체는 호출자가 시작한 같은 트랜잭션 안에서 처리한다.
    if (create)
    {
        std::unique_ptr<sql::PreparedStatement> insert(_connection.prepareStatement("INSERT INTO character_inventories (character_id) VALUES (?)"));
        insert->setUInt(1, id); insert->executeUpdate();
    }
    std::unique_ptr<sql::PreparedStatement> header(_connection.prepareStatement(
        "UPDATE character_inventories SET revision = ?, mesos = ?, hp = ?, max_hp = ?, mp = ?, max_mp = ? WHERE character_id = ?"));
    header->setUInt64(1, state.revision); header->setUInt64(2, state.mesos); header->setUInt(3, state.hp); header->setUInt(4, state.maxHp);
    header->setUInt(5, state.mp); header->setUInt(6, state.maxMp); header->setUInt(7, id); header->executeUpdate();
    std::unique_ptr<sql::PreparedStatement> remove(_connection.prepareStatement("DELETE FROM character_inventory_slots WHERE character_id = ?"));
    remove->setUInt(1, id); remove->executeUpdate();
    std::unique_ptr<sql::PreparedStatement> insert(_connection.prepareStatement(
        "INSERT INTO character_inventory_slots (character_id, category, slot, item_id, quantity) VALUES (?, ?, ?, ?, ?)"));
    for (uint32_t i = 0; i < 120; ++i)
    {
        const auto& entry = state.entries[i]; if (!entry.itemId) continue;
        insert->setUInt(1, id); insert->setUInt(2, i / 24); insert->setUInt(3, i % 24);
        insert->setUInt(4, entry.itemId); insert->setUInt(5, entry.quantity); insert->executeUpdate();
    }
}
InventorySnapshotPacket InventoryRepository::Apply(uint32_t accountId, uint32_t id, const InventoryRequestPacket& request,
    const std::shared_ptr<std::atomic<bool>>& canceled, std::chrono::steady_clock::time_point deadline)
{
    InventorySnapshotPacket failure; failure.result = InventoryResult::StorageError;
    auto expired = [&] { return canceled->load() || std::chrono::steady_clock::now() >= deadline; };
    if (!id || !accountId || expired()) return failure;
    try
    {
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
            Write(id, result.result == InventoryResult::Success ? result : original, !exists);
        if (expired()) return failure;
        // 커밋 직전까지 확인한 취소·시간 초과 요청은 위의 조기 반환으로 롤백한다.
        transaction.Commit(); return result;
    }
    catch (const std::exception& error)
    { std::cerr << "Inventory database transaction failed: " << error.what() << '\n'; return failure; }
}
