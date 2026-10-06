#include "../ServerCore/SocketUtils.h"
#include "../ServerCore/Session.h"
#include "../ServerCore/NetAddress.h"
#include "../ServerCore/Listener.h"
#include "../ServerCore/IocpCore.h"
#include "../ServerCore/IocpEvent.h"
#include "../ServerCore/IocpWorker.h"
#include "../ServerCore/SessionManager.h"
#include "../ServerCore/ServerListenConfig.h"
#include "../ServerCore/DatabaseConnection.h"
#include "AuthTicketManager.h"
#include "CharacterRepository.h"
#include "GameSession.h"
#include "MapManager.h"
#include "Monster.h"
#include "PlayerManager.h"
#include "ServerSession.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace
{
    std::string GetEnvironmentVariable(const char* name)
    {
        char* value = nullptr;
        size_t length = 0;

        if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
        {
            std::cerr << "Environment variable is missing: " << name << '\n';
            return {};
        }

        std::string result(value);
        free(value);
        return result;
    }
}

int main(int argc, char* argv[])
{
    // 테스트 서버를 강제 종료해도 마지막 진단 로그가 파일에 남도록 즉시 출력한다.
    std::cout << std::unitbuf;
    ServerListenConfig listenConfig;
    std::string configError;
    if (!ServerListenConfig::Load(listenConfig, configError))
    {
        std::cerr << configError << '\n';
        return 1;
    }
    MapManager mapManager;
    std::filesystem::path mapPath = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::absolute(argv[0]).parent_path() / "data" / "maps.csv";
    if (!mapManager.LoadMaps(mapPath.string()))
        return 1;

    Map* startMap = mapManager.FindMap(100000000);
    if (startMap == nullptr)
    {
        std::cerr << "Start map is missing: 100000000\n";
        return 1;
    }

    // 지형 검증을 완료한 Map만 실제 이동 시뮬레이션에 사용한다.
    if (!mapManager.LoadGeometry(mapPath.parent_path().string()))
        return 1;

    const std::string dbHost = GetEnvironmentVariable("DB_HOST");
    const std::string dbUser = GetEnvironmentVariable("DB_USER");
    const std::string dbPassword = GetEnvironmentVariable("DB_PASSWORD");
    const std::string dbName = GetEnvironmentVariable("DB_NAME");

    if (dbHost.empty() || dbUser.empty() || dbPassword.empty() || dbName.empty())
        return 1;

    DatabaseConnection database;
    if (!database.Connect(dbHost, dbUser, dbPassword, dbName))
        return 1;

    if (!database.TestConnection())
        return 1;

    CharacterRepository characterRepository(*database.GetConnection());

    if (!SocketUtils::Init())
        return 1;

    IocpCore iocp;
    if (!iocp.IsValid())
    {
        SocketUtils::Clear();
        return 1;
    }

    // 7777: LoginServer에서 authKey를 받은 게임 클라이언트가 접속하는 포트.
    // 7778: LoginServer가 게임 입장용 인증 티켓을 등록하는 서버 간 통신 포트.
    NetAddress gameAddress(listenConfig.GetClientBindIp(), 7777);
    // 인증 티켓 등록은 같은 PC의 LoginServer만 접근하는 로컬 연결로 유지한다.
    NetAddress serverAddress(L"127.0.0.1", 7778);

    Listener gameListener;
    Listener serverListener;

    if (!gameListener.Start(gameAddress))
    {
        std::cerr << "GameServer startup failed on " << listenConfig.clientBindIp << ":7777\n";
        SocketUtils::Clear();
        return 1;
    }

    if (!serverListener.Start(serverAddress))
    {
        std::cerr << "GameServer ticket listener startup failed on 127.0.0.1:7778\n";
        gameListener.Close();
        SocketUtils::Clear();
        return 1;
    }

    if (!iocp.Register(reinterpret_cast<HANDLE>(gameListener.GetSocket()), reinterpret_cast<ULONG_PTR>(&gameListener)))
    {
        gameListener.Close();
        serverListener.Close();
        SocketUtils::Clear();
        return 1;
    }

    if (!iocp.Register(reinterpret_cast<HANDLE>(serverListener.GetSocket()), reinterpret_cast<ULONG_PTR>(&serverListener)))
    {
        gameListener.Close();
        serverListener.Close();
        SocketUtils::Clear();
        return 1;
    }

    if (!gameListener.PostAccept())
    {
        gameListener.Close();
        serverListener.Close();
        SocketUtils::Clear();
        return 1;
    }

    if (!serverListener.PostAccept())
    {
        gameListener.Close();
        serverListener.Close();
        SocketUtils::Clear();
        return 1;
    }

    SessionManager sessionManager;
    AuthTicketManager authTicketManager;
    PlayerManager playerManager;
    Monster testMonster(1, 50, 20);
    if (!startMap->AddMonster(testMonster))
        return 1;

    IocpWorker worker(iocp, sessionManager);

    worker.RegisterListener(gameListener,
        [&authTicketManager, &characterRepository, &playerManager, &mapManager](SOCKET socket)
        {
            return std::make_unique<GameSession>(socket, authTicketManager, characterRepository, playerManager, mapManager);
        });

    worker.RegisterListener(serverListener,
        [&authTicketManager](SOCKET socket)
        {
            return std::make_unique<ServerSession>(socket, authTicketManager);
        });

    std::cout << "GameServer listening on " << listenConfig.clientBindIp << ":7777\n";
    std::cout << "GameServer ticket listener on 127.0.0.1:7778\n";

    auto nextCleanup = std::chrono::steady_clock::now();
    auto nextMetrics = nextCleanup + std::chrono::seconds(5);
    while (true)
    {
        mapManager.Advance();
        if (!worker.Dispatch(mapManager.GetWaitMilliseconds()))
        {
            gameListener.Close();
            serverListener.Close();
            SocketUtils::Clear();
            return 1;
        }

        sessionManager.Cleanup();
        if (std::chrono::steady_clock::now() >= nextMetrics)
        {
            mapManager.LogMetrics(sessionManager.GetSessionCount(), sessionManager.GetQueuedSendBytes());
            nextMetrics = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        }
        if (std::chrono::steady_clock::now() >= nextCleanup)
        {
            authTicketManager.CleanupExpired();
            playerManager.GetChatLimiter().Cleanup();
            nextCleanup = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        }
    }

    gameListener.Close();
    serverListener.Close();
    SocketUtils::Clear();
    return 0;
}
