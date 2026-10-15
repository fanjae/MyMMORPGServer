#pragma once
#include "../Protocol/FieldPacket.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <functional>
#include "../Protocol/TradePacket.h"
namespace sql { class Connection; }

// 캐릭터 조회와 같은 DB 작업 스레드에서 공유 Connection을 순서대로 사용한다.
class InventoryRepository
{
public:
    explicit InventoryRepository(sql::Connection& connection, std::function<void()> afterCommit = {});
    InventorySnapshotPacket Apply(uint32_t accountId, uint32_t characterId, const InventoryRequestPacket& request,
        const std::shared_ptr<std::atomic<bool>>& canceled, std::chrono::steady_clock::time_point deadline, InventoryMutation mutation = {});
    bool TryReconnect();
    TradeCommitResult Exchange(const TradeCommitRequest& request);
    TradeLookupResult LookupTrade(uint32_t account,uint32_t character,uint64_t tradeId);
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
    // 검사에서만 커밋 이후 결과 유실을 주입한다. 실제 서버에서는 콜백을 등록하지 않는다.
    std::function<void()> _afterCommit;
};
