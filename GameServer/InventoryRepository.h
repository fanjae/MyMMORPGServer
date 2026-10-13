#pragma once
#include "../Protocol/FieldPacket.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
namespace sql { class Connection; }

// 캐릭터 조회와 같은 DB 작업 스레드에서 공유 Connection을 순서대로 사용한다.
class InventoryRepository
{
public:
    explicit InventoryRepository(sql::Connection& connection);
    InventorySnapshotPacket Apply(uint32_t accountId, uint32_t characterId, const InventoryRequestPacket& request,
        const std::shared_ptr<std::atomic<bool>>& canceled, std::chrono::steady_clock::time_point deadline, InventoryMutation mutation = {});
    bool TryReconnect();
    void ConnectionFailed(int code, const std::string& state);
    struct WriteMetrics { uint32_t headers = 0, slots = 0; };
    WriteMetrics LastWriteMetrics() const { return _writes; }
private:
    bool Read(uint32_t id, InventorySnapshotPacket& state);
    void Write(uint32_t id, const InventorySnapshotPacket& original, const InventorySnapshotPacket& state, bool create);
    void CleanupRecords();
    sql::Connection& _connection;
    std::string _schema;
    bool _reconnect = false;
    WriteMetrics _writes;
    std::chrono::steady_clock::time_point _nextCleanup{};
};
