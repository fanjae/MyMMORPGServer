#include "../../GameServer/Map.h"
#include "../../GameServer/Player.h"
#include "../../ServerCore/Session.h"
#include "../../ServerCore/SocketUtils.h"
#include "../../ServerCore/NetAddress.h"
#include "../../ServerCore/BlockingSocket.h"
#include <iostream>
#include <memory>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <filesystem>

namespace
{
    class RelaySession : public Session
    {
    public:
        explicit RelaySession(SOCKET socket) : Session(socket) {}
        bool Send(const char* data, int32_t size) override
        {
            return BlockingSocket::Transfer(GetSocket(), const_cast<char*>(data), size, true,
                std::chrono::steady_clock::now() + std::chrono::seconds(2));
        }
        std::vector<char> received;
        std::unique_ptr<Player> player;
    protected:
        bool OnPacket(uint16_t, const char*, uint16_t) override { return true; }
    };
}

// DB 없이 실제 production Map 중계를 C# 클라이언트와 TCP로 검증하는 loopback 전용 서버.
// 인증을 생략하는 테스트 전용 모드이며 외부 인터페이스에는 바인딩하지 않는다.
int RunMovementRelayFixture(uint16_t port, bool threeD)
{
    std::cout << std::unitbuf;
    SOCKET listener = SocketUtils::CreateSocket();
    struct ListenerGuard { SOCKET& socket; ~ListenerGuard() { SocketUtils::Close(socket); } } guard{listener};
    if (!SocketUtils::Bind(listener, NetAddress(L"127.0.0.1", port)) || !SocketUtils::Listen(listener))
        throw std::runtime_error("Fixture listener startup failed");
    MapGeometry geometry;
    geometry.map = {100000000, 0, 0, -400, 400, -200, 200, 80, 12};
    geometry.movement = {1, MovementMode::Platformer, 1, 6, 6, 80, 160, 400, 300};
    geometry.footholds.push_back({1, -400, -6, 400, -6, 0, 0});
    Map map{geometry.map}; map.SetGeometry(geometry);
    std::unordered_map<uint32_t, World3DDefinition> worlds;
    std::unique_ptr<Map> map3D;
    if (threeD)
    {
        std::unordered_map<uint32_t, MapDefinition> definitions{
            {100000002, {100000002, 0, 0, -400, 400, -200, 400, 80, 12}},
            {100000003, {100000003, -200, 0, -400, 400, -200, 400, 80, 12}}};
        std::string error;
        auto data = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "data";
        if (!World3DLoader::Load(data.string(), definitions, worlds, error)) throw std::runtime_error(error);
        map3D = std::make_unique<Map>(definitions.at(100000002)); map3D->SetWorld3D(worlds.at(100000002));
    }
    Map& activeMap = threeD ? *map3D : map;
    std::vector<std::unique_ptr<RelaySession>> sessions;
    auto start = std::chrono::steady_clock::now();
    uint64_t tick = 0;
    uint32_t acceptedConnections = 0;
    std::cout << "READY movement relay fixture 127.0.0.1:" << port << '\n';
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(120))
    {
        fd_set reads; FD_ZERO(&reads); FD_SET(listener, &reads);
        for (const auto& session : sessions) FD_SET(session->GetSocket(), &reads);
        timeval wait{0, 10000};
        if (select(0, &reads, nullptr, nullptr, &wait) == SOCKET_ERROR)
            throw std::runtime_error("Fixture select failed");
        if (FD_ISSET(listener, &reads))
        {
            SOCKET socket = accept(listener, nullptr, nullptr);
            if (socket == INVALID_SOCKET || acceptedConnections >= 2)
            {
                SocketUtils::Close(socket);
                throw std::runtime_error("Fixture accepts exactly two clients");
            }
            auto session = std::make_unique<RelaySession>(socket);
            uint32_t id = ++acceptedConnections == 1 ? 1001 : 2001;
            session->player = std::make_unique<Player>(id, id, "relay-test", static_cast<uint16_t>(1));
            session->player->SetSession(session.get());
            if (!activeMap.AddPlayer(*session->player)) throw std::runtime_error("Fixture map entry failed");
            EnterGameResponse enter; enter.result = EnterGameResult::Success; enter.characterId = id;
            CopyCharacterName(enter.name, "relay-test"); enter.level = 1;
            PacketHeader header{static_cast<uint16_t>(sizeof(PacketHeader) + sizeof(enter)), static_cast<uint16_t>(GamePacketOpcode::EnterGameResponse)};
            char buffer[sizeof(header) + sizeof(enter)]; memcpy(buffer, &header, sizeof(header)); memcpy(buffer + sizeof(header), &enter, sizeof(enter));
            if (!session->Send(buffer, sizeof(buffer)) || !activeMap.SendMapInfo(*session->player) || !activeMap.SendGeometry(*session->player) ||
                !activeMap.NotifyPlayerEntered(*session->player)) throw std::runtime_error("Fixture bootstrap failed");
            sessions.push_back(std::move(session));
        }
        for (auto it = sessions.begin(); it != sessions.end();)
        {
            RelaySession& session = **it;
            if (!FD_ISSET(session.GetSocket(), &reads)) { ++it; continue; }
            char incoming[4096]; int received = recv(session.GetSocket(), incoming, sizeof(incoming), 0);
            if (received <= 0)
            {
                activeMap.NotifyPlayerLeaving(*session.player); activeMap.RemovePlayer(*session.player);
                it = sessions.erase(it); continue;
            }
            session.received.insert(session.received.end(), incoming, incoming + received);
            while (session.received.size() >= sizeof(PacketHeader))
            {
                PacketHeader frame; memcpy(&frame, session.received.data(), sizeof(frame));
                if (threeD && frame.opcode == static_cast<uint16_t>(GamePacketOpcode::EnterGameRequest))
                {
                    if (frame.size != sizeof(PacketHeader) + sizeof(EnterGameRequest)) throw std::runtime_error("Invalid 3D fixture handshake");
                    if (session.received.size() < frame.size) break;
                    EnterGameRequest request; memcpy(&request, session.received.data() + sizeof(frame), sizeof(request));
                    if (request.protocolVersion != GAME3D_PROTOCOL_VERSION) throw std::runtime_error("3D fixture protocol mismatch");
                    session.received.erase(session.received.begin(), session.received.begin() + frame.size); continue;
                }
                if (frame.size < sizeof(frame) + sizeof(MovementActionsHeader) || frame.size > MAX_PACKET_SIZE ||
                    frame.opcode != static_cast<uint16_t>(threeD ? GamePacketOpcode::MovementActions3D : GamePacketOpcode::MovementActions))
                    throw std::runtime_error("Fixture invalid action frame");
                if (session.received.size() < frame.size) break;
                MovementActionsHeader header; memcpy(&header, session.received.data() + sizeof(frame), sizeof(header));
                if (header.count == 0 || header.count > MAX_MOVEMENT_ACTIONS || frame.size != sizeof(frame) + sizeof(header) + header.count * (threeD ? sizeof(MovementAction3D) : sizeof(MovementAction)))
                    throw std::runtime_error("Fixture invalid action count");
                if (threeD)
                {
                    MovementAction3D actions[MAX_MOVEMENT_ACTIONS];
                    memcpy(actions, session.received.data() + sizeof(frame) + sizeof(header), header.count * sizeof(MovementAction3D));
                    activeMap.ReceiveActions3D(*session.player, header, actions);
                }
                else
                {
                    MovementAction actions[MAX_MOVEMENT_ACTIONS];
                    memcpy(actions, session.received.data() + sizeof(frame) + sizeof(header), header.count * sizeof(MovementAction));
                    activeMap.ReceiveMovementActions(*session.player, header, actions);
                }
                session.received.erase(session.received.begin(), session.received.begin() + frame.size);
            }
            ++it;
        }
        auto now = std::chrono::steady_clock::now();
        uint64_t elapsedTick = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() / 20);
        if (elapsedTick > tick) { tick = elapsedTick; activeMap.Tick(tick, now); }
        if (acceptedConnections == 2 && sessions.empty())
        {
            std::cout << "DONE movement relay fixture\n";
            return 0;
        }
    }
    throw std::runtime_error("Fixture timeout");
}
