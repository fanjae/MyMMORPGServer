#include "GameSession.h"
#include "AuthTicketManager.h"
#include "CharacterRepository.h"
#include "GamePacketHandler.h"
#include "Map.h"
#include "Player.h"
#include "PlayerManager.h"
#include "FieldIdentity.h"
#include <algorithm>

#include <iostream>
#include <utility>

GameSession::GameSession(SOCKET socket, AuthTicketManager& authTicketManager, CharacterRepository& characterRepository, PlayerManager& playerManager, MapManager& mapManager) : Session(socket), _authTicketManager(authTicketManager), _characterRepository(characterRepository), _playerManager(playerManager), _mapManager(mapManager)
{
}

GameSession::~GameSession() = default;

bool GameSession::BeginCharacterLoad(const AuthTicket& ticket)
{
    _loadCanceled = std::make_shared<std::atomic<bool>>(false);
    _characterLoad = _characterRepository.FindByIdAsync(ticket.accountId, ticket.characterId, _loadCanceled);
    if (!_characterLoad.valid())
        return false;
    _pendingTicket = ticket;
    _loadDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    return true;
}

void GameSession::Update()
{
    // 장기 장애에서는 기록 보관 기간을 넘겨 재시도하지 않고 연결을 종료해 저장 상태로 재접속하게 한다.
    if (_recovering && std::chrono::steady_clock::now() >= _recoveryDeadline) { RequestClose(); return; }
    // DB 완료 여부만 확인하고 입장 처리와 응답 전송은 IOCP 실행 스레드에서 수행한다.
    if (_characterLoad.valid())
    {
        if (std::chrono::steady_clock::now() >= _loadDeadline) { RequestClose(); return; }
        if (_characterLoad.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            CharacterLoadResult result;
            try { result = _characterLoad.get(); }
            catch (const std::exception& exception) { std::cerr << "Character load worker failed: " << exception.what() << '\n'; }
            if (!GamePacketHandler::CompleteEnterGame(*this, _pendingTicket, std::move(result))) RequestClose();
        }
    }
    PumpMutation();
    if (!_inventoryRequest.valid()) return;
    if (std::chrono::steady_clock::now() >= _inventoryDeadline)
    {
        // 제한 시간을 넘긴 결과는 적용하지 않고 작업 스레드에도 취소를 전달한다.
        _inventoryCanceled->store(true); _inventoryRequest = {};
        FinishInventory(InventoryError(InventoryResult::StorageError));
        return;
    }
    if (_inventoryRequest.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto result = InventoryError(InventoryResult::StorageError);
    try { result = _inventoryRequest.get(); }
    catch (const std::exception& exception) { std::cerr << "Inventory worker failed: " << exception.what() << '\n'; }
    FinishInventory(result); PumpMutation();
}

bool GameSession::BeginInventoryRequest(const InventoryRequestPacket& request)
{
    // 연결당 한 요청만 진행하고 연속 요청은 최소 100ms 간격으로 제한한다.
    auto now = std::chrono::steady_clock::now();
    if (FieldBusy() || now < _nextInventoryRequest) return false;
    _activeMutation = {};
    _inventoryRequestId=request.requestId;
    _nextInventoryRequest = now + std::chrono::milliseconds(100);
    _inventoryCanceled = std::make_shared<std::atomic<bool>>(false);
    _inventoryRequest = _characterRepository.ApplyInventoryAsync(_accountId, _characterId, request, _inventoryCanceled);
    _inventoryDeadline = now + std::chrono::seconds(5);
    return _inventoryRequest.valid();
}
InventorySnapshotPacket GameSession::InventoryError(InventoryResult result) const
{
    auto snapshot = _lastInventory; snapshot.result = result; snapshot.requestId=0; return snapshot;
}

bool GameSession::QueueFieldMutation(InventoryMutation mutation, Map* dropMap)
{
    if (!_player || !IsAuthenticated() || _mutations.size() >= 8 || _recovering || (!mutation.damage && FieldBusy())) return false;
    if (mutation.queryDrop)
    {
        auto now = std::chrono::steady_clock::now();
        if (now < _nextInventoryRequest) return false;
        _nextInventoryRequest = now + std::chrono::milliseconds(100);
    }
    if (mutation.damage && !mutation.mutationId) mutation.mutationId = NewFieldOperationId();
    _mutations.push_back({mutation,dropMap}); PumpMutation(); return true;
}
void GameSession::PumpMutation()
{
    if (_inventoryRequest.valid() || _mutations.empty() || !_player) return;
    if (_recovering && std::chrono::steady_clock::now() < _recoveryAt) return;
    _activeMutation = _mutations.front(); _mutations.pop_front();
    _inventoryRequestId=0;
    _inventoryCanceled=std::make_shared<std::atomic<bool>>(false);
    _inventoryRequest=_characterRepository.ApplyInventoryAsync(_accountId,_characterId,{},_inventoryCanceled,_activeMutation.mutation);
    _inventoryDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    if (!_inventoryRequest.valid()) FinishInventory(InventoryError(InventoryResult::StorageError));
}
void GameSession::FinishInventory(InventorySnapshotPacket result)
{
    auto pending=_activeMutation; _activeMutation={};
    bool recovered = _recovering && pending.mutation.damage && result.result == InventoryResult::Success;
    uint64_t requestId=_inventoryRequestId; _inventoryRequestId=0;
    uint16_t actualDamage=pending.mutation.damage && _lastInventory.hp>result.hp ? uint16_t(_lastInventory.hp-result.hp) : 0;
    if (result.result==InventoryResult::StorageError)
    {
        result=InventoryError(result.result);
        if (pending.mutation.damage && _player)
        {
            // 응답 유실·커밋 결과 불명확도 같은 피격 번호로 재조회한다. 재시도 중 신규 전투는 중단한다.
            if (!_recovering) _recoveryDeadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
            _recovering = true; _player->PauseCombat(true); _player->InvalidateVitals();
            _mutations.push_front(pending);
            _recoveryAt = std::chrono::steady_clock::now() + std::chrono::seconds(1u << (std::min)(_recoveryAttempts++, 3u));
            _player->GetMap()->SendVitals(*_player);
            return;
        }
    }
    result.requestId=requestId;
    if (result.result!=InventoryResult::StorageError) { _lastInventory=result; if (_player) _player->SetVitals(result); }
    if (pending.mutation.damage && result.result == InventoryResult::Success && _recovering)
    {
        _recovering = false; _recoveryAttempts = 0; _player->PauseCombat(false);
        // 복구 후에는 슬롯도 재조회 결과로 맞추고 더 이상 이전 조작을 재실행하지 않는다.
        if (!GamePacketHandler::SendInventorySnapshot(*this, result)) RequestClose();
    }
    if (pending.mutation.queryDrop)
    {
        PickupResult status = result.result == InventoryResult::Success ? PickupResult::Success :
            result.result == InventoryResult::AlreadyTaken ? PickupResult::AlreadyTaken :
            result.result == InventoryResult::InvalidSlot ? PickupResult::NoRecord : PickupResult::StorageError;
        if (!GamePacketHandler::SendPickupResult(*this, pending.mutation.dropId, status)) RequestClose();
    }
    if (pending.mutation.dropId && pending.dropMap)
    {
        pending.dropMap->FinishDrop(pending.mutation.dropId,_characterId,result.result);
        PickupResult status=result.result==InventoryResult::Success ? PickupResult::Success : result.result==InventoryResult::Full ? PickupResult::Full :
            result.result==InventoryResult::AlreadyTaken ? PickupResult::AlreadyTaken : PickupResult::StorageError;
        if (!GamePacketHandler::SendPickupResult(*this,pending.mutation.dropId,status)) RequestClose();
    }
    if (!IsConnected() || !IsAuthenticated() || !_player) return;
    if (pending.mutation.revive && result.result==InventoryResult::Success)
    {
        Map& map=*_player->GetMap(); map.NotifyPlayerLeaving(*_player); map.RemovePlayer(*_player); map.AddPlayer(*_player);
        if (!map.SendMapInfo(*_player) || !map.SendGeometry(*_player) || !map.NotifyPlayerEntered(*_player)) RequestClose();
    }
    if (pending.mutation.damage && result.result==InventoryResult::Success)
    {
        // 피격은 작은 생명력 패킷만 보내고 인벤토리 슬롯은 변경하지 않는다.
        _player->GetMap()->SendVitals(*_player,actualDamage,recovered);
    }
    else if (!GamePacketHandler::SendInventorySnapshot(*this,result)) RequestClose();
}

void GameSession::SetPlayer(std::unique_ptr<Player> player)
{
    player->SetDamageHandler([this](uint16_t damage) { InventoryMutation mutation; mutation.damage=damage; return QueueFieldMutation(mutation); });
    _player = std::move(player);
}

bool GameSession::OnPacket(uint16_t opcode, const char* payload, uint16_t payloadSize)
{
    return GamePacketHandler::Handle(*this, opcode, payload, payloadSize);
}

void GameSession::OnDisconnected()
{
    // 이전 연결의 DB 결과는 버리고 새 연결의 인증과 Player 상태에 적용하지 않는다.
    if (_loadCanceled != nullptr)
        _loadCanceled->store(true);
    _characterLoad = {};
    _pendingTicket = {};
    // 연결이 종료되면 대기 중인 인벤토리 작업도 취소하고 이후 결과는 버린다.
    if (_inventoryCanceled != nullptr) _inventoryCanceled->store(true);
    _inventoryRequest = {};
    if (_activeMutation.dropMap) _activeMutation.dropMap->FinishDrop(_activeMutation.mutation.dropId,_characterId,InventoryResult::StorageError);
    _activeMutation={}; _mutations.clear(); _recovering=false; _recoveryAttempts=0;
    if (_player != nullptr)
    {
        if (Map* map = _player->GetMap())
        {
            map->NotifyPlayerLeaving(*_player);
            map->RemovePlayer(*_player);
        }

        _playerManager.Remove(*_player);
        _player->SetSession(nullptr);

        std::cout << "Player Left Game: accountId=" << _player->GetAccountId()
            << " characterId=" << _player->GetCharacterId()
            << " name=" << _player->GetName() << '\n';
    }

    _authenticated = false;
    _accountId = 0;
    _characterId = 0;
    _player.reset();
}
