#pragma once
#include "../../GameServer/InventoryService.h"
#include <unordered_map>

// DB 없이 규칙과 TCP를 검증하는 테스트 전용 저장소. 실제 세션은 InventoryRepository를 사용한다.
class TestInventoryStore
{
public:
    InventorySnapshotPacket Apply(uint32_t id, const InventoryRequestPacket& request)
    {
        auto found = _states.find(id);
        if (found == _states.end()) found = _states.emplace(id, InventoryService::Starter()).first;
        auto reply = InventoryService::Apply(found->second, request);
        if (reply.result == InventoryResult::Success) found->second = reply;
        return reply;
    }
private:
    std::unordered_map<uint32_t, InventorySnapshotPacket> _states;
};
