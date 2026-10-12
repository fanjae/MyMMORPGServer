#include "GameSession.h"
#include "AuthTicketManager.h"
#include "CharacterRepository.h"
#include "GamePacketHandler.h"
#include "Map.h"
#include "Player.h"
#include "PlayerManager.h"

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
    if (!_inventoryRequest.valid()) return;
    if (std::chrono::steady_clock::now() >= _inventoryDeadline)
    {
        // 제한 시간을 넘긴 결과는 적용하지 않고 작업 스레드에도 취소를 전달한다.
        _inventoryCanceled->store(true); _inventoryRequest = {};
        if (!GamePacketHandler::SendInventorySnapshot(*this, InventoryError(InventoryResult::StorageError))) RequestClose();
        return;
    }
    if (_inventoryRequest.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto result = InventoryError(InventoryResult::StorageError);
    try { result = _inventoryRequest.get(); }
    catch (const std::exception& exception) { std::cerr << "Inventory worker failed: " << exception.what() << '\n'; }
    // 저장 실패 응답은 마지막으로 확인한 상태를 유지해 화면이 기본 상태로 바뀌지 않도록 한다.
    if (result.result == InventoryResult::StorageError) result = InventoryError(result.result);
    else { _lastInventory = result; if (_player != nullptr) _player->SetVitals(result); }
    if (IsConnected() && IsAuthenticated() && !GamePacketHandler::SendInventorySnapshot(*this, result)) RequestClose();
}

bool GameSession::BeginInventoryRequest(const InventoryRequestPacket& request)
{
    // 연결당 한 요청만 진행하고 연속 요청은 최소 100ms 간격으로 제한한다.
    auto now = std::chrono::steady_clock::now();
    if (_inventoryRequest.valid() || now < _nextInventoryRequest) return false;
    _nextInventoryRequest = now + std::chrono::milliseconds(100);
    _inventoryCanceled = std::make_shared<std::atomic<bool>>(false);
    _inventoryRequest = _characterRepository.ApplyInventoryAsync(_accountId, _characterId, request, _inventoryCanceled);
    _inventoryDeadline = now + std::chrono::seconds(5);
    return _inventoryRequest.valid();
}
InventorySnapshotPacket GameSession::InventoryError(InventoryResult result) const
{
    auto snapshot = _lastInventory; snapshot.result = result; return snapshot;
}

void GameSession::SetPlayer(std::unique_ptr<Player> player)
{
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
