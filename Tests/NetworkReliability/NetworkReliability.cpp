#include "../../ServerCore/Session.h"
#include "../../ServerCore/SocketUtils.h"
#include "../../ServerCore/BlockingSocket.h"
#include "../../ServerCore/IocpCore.h"
#include "../../ServerCore/IocpWorker.h"
#include "../../ServerCore/Listener.h"
#include "../../ServerCore/NetAddress.h"
#include "../../ServerCore/ServerListenConfig.h"
#include "../../ServerCore/SessionManager.h"
#include "../../GameServer/Map.h"
#include "../../GameServer/Player.h"
#include "../../LoginServer/GameServerClient.h"
#include "../../Protocol/CharacterName.h"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    class SocketPair
    {
    public:
        SocketPair()
        {
            listener = SocketUtils::CreateSocket();
            Check(SocketUtils::Bind(listener, NetAddress(L"127.0.0.1", 0)) && SocketUtils::Listen(listener), "Socket listener failed");
            sockaddr_in address{};
            int size = sizeof(address);
            Check(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) == 0, "Socket address failed");
            port = ntohs(address.sin_port);
            client = SocketUtils::CreateSocket();
            Check(BlockingSocket::Connect(client, address, std::chrono::steady_clock::now() + std::chrono::seconds(2)), "Socket connect failed");
            server = accept(listener, nullptr, nullptr);
            Check(server != INVALID_SOCKET, "Socket accept failed");
        }
        ~SocketPair() { SocketUtils::Close(server); SocketUtils::Close(client); SocketUtils::Close(listener); }
        SOCKET ReleaseServer() { SOCKET socket = server; server = INVALID_SOCKET; return socket; }
        SOCKET server = INVALID_SOCKET;
        SOCKET client = INVALID_SOCKET;
        SOCKET listener = INVALID_SOCKET;
        uint16_t port = 0;
    };

    class PacketSession : public Session
    {
    public:
        explicit PacketSession(SOCKET socket) : Session(socket) {}
        int packets = 0;
        int disconnects = 0;
    protected:
        bool OnPacket(uint16_t, const char*, uint16_t) override { ++packets; return true; }
        void OnDisconnected() override { ++disconnects; }
    };

    DWORD Dispatch(IocpCore& iocp, Session& session)
    {
        DWORD bytes;
        ULONG_PTR key;
        OVERLAPPED* overlapped;
        bool success, timeout;
        Check(iocp.GetCompletion(bytes, key, overlapped, success, timeout, 2000) && !timeout, "Completion timed out");
        Check(key == reinterpret_cast<ULONG_PTR>(&session), "Wrong completion owner");
        if (!session.Dispatch(reinterpret_cast<IocpEvent*>(overlapped), bytes, success))
            session.Close();
        return bytes;
    }

    void Drain(IocpCore& iocp, Session& session)
    {
        session.Close();
        while (!session.CanDestroy())
            Dispatch(iocp, session);
    }

    class RecordingSession : public Session
    {
    public:
        RecordingSession() : Session(SocketUtils::CreateSocket()) {}
        bool Send(const char*, int32_t) override
        {
            if (fail || !IsConnected())
            {
                RequestClose();
                return false;
            }
            ++packets;
            return true;
        }
        bool fail = false;
        int packets = 0;
        Player* player = nullptr;
    protected:
        bool OnPacket(uint16_t, const char*, uint16_t) override { return true; }
        void OnDisconnected() override
        {
            if (player != nullptr && player->GetMap() != nullptr)
            {
                Map* map = player->GetMap();
                map->NotifyPlayerLeaving(*player);
                map->RemovePlayer(*player);
            }
        }
    };

    void MapFailure(const std::function<bool(Map&, Player&)>& notify)
    {
        MapDefinition definition{100000000, 0, 0, -400, 400, -200, 200, 80, 12};
        Map map(definition);
        Player sender(1, 1, "sender", 1), failed(2, 2, "failed", 1), healthy(3, 3, "healthy", 1);
        SessionManager manager;
        auto a = std::make_unique<RecordingSession>();
        auto b = std::make_unique<RecordingSession>();
        auto c = std::make_unique<RecordingSession>();
        RecordingSession* senderSession = a.get();
        RecordingSession* failedSession = b.get();
        RecordingSession* healthySession = c.get();
        sender.SetSession(a.get()); a->player = &sender;
        failed.SetSession(b.get()); b->player = &failed;
        healthy.SetSession(c.get()); c->player = &healthy;
        manager.Add(std::move(a)); manager.Add(std::move(b)); manager.Add(std::move(c));
        Check(map.AddPlayer(sender) && map.AddPlayer(failed) && map.AddPlayer(healthy), "Map registration failed");
        failedSession->fail = true;
        Check(notify(map, sender), "Recipient failure propagated to sender");
        Check(senderSession->IsConnected() && healthySession->packets > 0 && map.FindPlayer(2) != nullptr, "Broadcast stopped or player removed during iteration");
        Check(failedSession->IsCloseRequested(), "Failed recipient not scheduled for cleanup");
        manager.Cleanup();
        Check(map.FindPlayer(2) == nullptr && map.FindPlayer(1) != nullptr && map.FindPlayer(3) != nullptr, "Cleanup removed healthy players");
    }
}

void RunChatTests();

int main(int argc, char* argv[])
{
    if (!SocketUtils::Init())
        return 1;
    try
    {
        RunChatTests();
        auto run = [](const char* name, const std::function<void()>& test) { test(); std::cout << "[PASS] " << name << '\n'; };
        run("Client listen config keeps local default and rejects invalid addresses", []
        {
            ServerListenConfig config;
            std::string error;
            Check(ServerListenConfig::TryParse(nullptr, config, error) && config.clientBindIp == "127.0.0.1", "Local default changed");
            Check(ServerListenConfig::TryParse("0.0.0.0", config, error) && config.clientBindIp == "0.0.0.0", "Wildcard config rejected");
            for (const char* invalid : {"", "not-an-ip", "256.1.1.1", "::1"})
                Check(!ServerListenConfig::TryParse(invalid, config, error) && !error.empty() && config.clientBindIp == "0.0.0.0", "Invalid bind address accepted or config replaced");
            Check(!NetAddress(L"not-an-ip", 0).IsValid(), "Invalid address silently became wildcard");
            Listener invalidListener;
            Check(!invalidListener.Start(NetAddress(L"not-an-ip", 0)), "Invalid address opened a listener");
        });
        run("Wildcard listener accepts connection through selected IPv4 interface", [&]
        {
            Listener listener;
            Check(listener.Start(NetAddress(L"0.0.0.0", 0)), "Wildcard listener failed");
            sockaddr_in address{};
            int size = sizeof(address);
            Check(getsockname(listener.GetSocket(), reinterpret_cast<sockaddr*>(&address), &size) == 0 && address.sin_addr.s_addr == INADDR_ANY, "Listener is not bound to all interfaces");
            std::string ip = argc > 1 ? argv[1] : "127.0.0.1";
            NetAddress destination(std::wstring(ip.begin(), ip.end()), ntohs(address.sin_port));
            Check(destination.IsValid(), "Test interface address is invalid");
            SOCKET client = SocketUtils::CreateSocket();
            bool connected = BlockingSocket::Connect(client, destination.GetAddress(), std::chrono::steady_clock::now() + std::chrono::seconds(2));
            if (!connected) { SocketUtils::Close(client); throw std::runtime_error("Interface connection failed"); }
            SOCKET accepted = accept(listener.GetSocket(), nullptr, nullptr);
            SocketUtils::Close(client);
            Check(accepted != INVALID_SOCKET, "Interface connection not accepted");
            SocketUtils::Close(accepted);
        });
        run("UTF-8 names preserve 16 characters and reject invalid data", []
        {
            char buffer[MAX_CHARACTER_NAME_LENGTH];
            std::string korean;
            std::string supplementary;
            for (int i = 0; i < 16; ++i) { korean += u8"가"; supplementary += "\xf0\x9f\x98\x80"; }
            Check(CopyCharacterName(buffer, korean) && korean == buffer, "Korean name changed");
            Check(CopyCharacterName(buffer, supplementary) && supplementary == buffer, "Four-byte name changed");
            Check(CopyCharacterName(buffer, std::string(16, 'a')), "16 ASCII characters rejected");
            for (const std::string& invalid : {std::string(17, 'a'), std::string("a\0b", 3), std::string("\xe3\x81"), std::string("\xc0\x80"), std::string("\xed\xa0\x80")})
                Check(!CopyCharacterName(buffer, invalid), "Invalid name accepted");
        });
        run("Chat recipient failure preserves sender and remaining broadcast", [] { MapFailure([](Map& map, Player& player) { return map.NotifyPlayerChat(player, "hello"); }); });
        run("Movement recipient failure preserves sender", [] { MapFailure([](Map& map, Player& player) { return map.NotifyPlayerMoved(player); }); });
        run("Entry recipient failure preserves new player", [] { MapFailure([](Map& map, Player& player) { return map.NotifyPlayerEntered(player); }); });
        run("Leave recipient failure is deferred safely", [] { MapFailure([](Map& map, Player& player) { map.NotifyPlayerLeaving(player); return true; }); });
        run("Maximum packet plus following packet survives fragmented TCP", []
        {
            SocketPair sockets;
            PacketSession session(sockets.ReleaseServer());
            IocpCore iocp;
            Check(iocp.Register(reinterpret_cast<HANDLE>(session.GetSocket()), reinterpret_cast<ULONG_PTR>(&session)) && session.PostRecv(), "Recv setup failed");
            std::vector<char> data(MAX_PACKET_SIZE + sizeof(PacketHeader), 'a');
            PacketHeader large{MAX_PACKET_SIZE, 42}, trailing{sizeof(PacketHeader), 43};
            memcpy(data.data(), &large, sizeof(large));
            memcpy(data.data() + MAX_PACKET_SIZE, &trailing, sizeof(trailing));
            Check(BlockingSocket::Transfer(sockets.client, data.data(), 4000, true, std::chrono::steady_clock::now() + std::chrono::seconds(2)), "Fragment send failed");
            for (DWORD received = 0; received < 4000;) received += Dispatch(iocp, session);
            Check(session.packets == 0, "Partial packet dispatched");
            Check(BlockingSocket::Transfer(sockets.client, data.data() + 4000, static_cast<int32_t>(data.size() - 4000), true, std::chrono::steady_clock::now() + std::chrono::seconds(2)), "Tail send failed");
            while (session.packets < 2 && session.IsConnected()) Dispatch(iocp, session);
            Check(session.IsConnected() && session.packets == 2, "Valid packet disconnected");
            Drain(iocp, session);
        });
        run("Closed session ignores queued successful receive completions", []
        {
            SocketPair sockets;
            PacketSession session(sockets.ReleaseServer());
            IocpCore iocp;
            Check(iocp.Register(reinterpret_cast<HANDLE>(session.GetSocket()), reinterpret_cast<ULONG_PTR>(&session)) && session.PostRecv(), "Recv setup failed");
            PacketHeader packet{sizeof(PacketHeader), 42};
            Check(BlockingSocket::Transfer(sockets.client, reinterpret_cast<char*>(&packet), sizeof(packet), true, std::chrono::steady_clock::now() + std::chrono::seconds(2)), "Send failed");
            Drain(iocp, session);
            Check(session.packets == 0 && session.disconnects == 1, "Closed session processed a packet");
        });
        run("Send queue overflow schedules only its owner for cleanup", []
        {
            SocketPair sockets;
            PacketSession session(sockets.ReleaseServer());
            IocpCore iocp;
            Check(iocp.Register(reinterpret_cast<HANDLE>(session.GetSocket()), reinterpret_cast<ULONG_PTR>(&session)), "Send setup failed");
            std::vector<char> packet(MAX_PACKET_SIZE);
            for (size_t bytes = 0; bytes < Session::MaxQueuedSendBytes; bytes += packet.size())
                Check(session.Send(packet.data(), static_cast<int32_t>(packet.size())), "Exact send queue limit rejected");
            Check(session.GetQueuedSendBytes() == Session::MaxQueuedSendBytes, "Queued bytes were not counted");
            Check(!session.Send(packet.data(), static_cast<int32_t>(packet.size())) && session.IsCloseRequested() && session.GetQueuedSendBytes() == Session::MaxQueuedSendBytes, "Queue overflow accepted");
            Drain(iocp, session);
        });
        run("Failed Accept completion rearms listener", []
        {
            IocpCore iocp;
            Listener listener;
            SessionManager manager;
            IocpWorker worker(iocp, manager);
            Check(listener.Start(NetAddress(L"127.0.0.1", 0)), "Listener failed");
            Check(iocp.Register(reinterpret_cast<HANDLE>(listener.GetSocket()), reinterpret_cast<ULONG_PTR>(&listener)), "Listener registration failed");
            PacketSession* accepted = nullptr;
            worker.RegisterListener(listener, [&](SOCKET socket) { auto session = std::make_unique<PacketSession>(socket); accepted = session.get(); return session; });
            AcceptEvent failed;
            Check(iocp.Post(reinterpret_cast<ULONG_PTR>(&listener), 0, &failed.overlapped) && worker.Dispatch(2000), "Accept failure stopped worker");
            sockaddr_in address{};
            int size = sizeof(address);
            getsockname(listener.GetSocket(), reinterpret_cast<sockaddr*>(&address), &size);
            SOCKET client = SocketUtils::CreateSocket();
            Check(BlockingSocket::Connect(client, address, std::chrono::steady_clock::now() + std::chrono::seconds(2)) && worker.Dispatch(2000) && accepted != nullptr, "Next connection not accepted");
            accepted->Close();
            while (!accepted->CanDestroy()) Check(worker.Dispatch(2000), "Pending recv cleanup failed");
            manager.Cleanup();
            SocketUtils::Close(client);
            listener.Close();
            // Listener의 취소 completion도 회수한 뒤 이벤트 메모리를 해제한다.
            DWORD bytes; ULONG_PTR key; OVERLAPPED* overlapped; bool success, timeout;
            Check(iocp.GetCompletion(bytes, key, overlapped, success, timeout, 2000) && !timeout, "Accept cancellation timed out");
            SocketUtils::Close(static_cast<AcceptEvent*>(reinterpret_cast<IocpEvent*>(overlapped))->acceptSocket);
        });
        run("Ticket registration times out while peer holds connection open", []
        {
            SocketPair sockets;
            std::thread peer([&] { SOCKET connection = accept(sockets.listener, nullptr, nullptr); std::this_thread::sleep_for(std::chrono::milliseconds(300)); SocketUtils::Close(connection); });
            auto start = std::chrono::steady_clock::now();
            bool accepted = GameServerClient(sockets.port, 100).RegisterAuthTicket(1, 1, 1);
            auto elapsed = std::chrono::steady_clock::now() - start;
            peer.join();
            Check(!accepted && elapsed < std::chrono::milliseconds(250), "Ticket registration ignored deadline");
        });
        SocketUtils::Clear();
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[FAIL] " << exception.what() << '\n';
        SocketUtils::Clear();
        return 1;
    }
}
