#pragma once
#include "../Protocol/FieldPacket.h"
#include <atomic>
#include <chrono>
#include <memory>
namespace sql { class Connection; }

// 캐릭터 조회와 같은 DB 작업 스레드에서 공유 Connection을 순서대로 사용한다.
class InventoryRepository
{
public:
    explicit InventoryRepository(sql::Connection& connection) : _connection(connection) {}
    InventorySnapshotPacket Apply(uint32_t accountId, uint32_t characterId, const InventoryRequestPacket& request,
        const std::shared_ptr<std::atomic<bool>>& canceled, std::chrono::steady_clock::time_point deadline);
private:
    bool Read(uint32_t id, InventorySnapshotPacket& state);
    void Write(uint32_t id, const InventorySnapshotPacket& state, bool create);
    sql::Connection& _connection;
};
