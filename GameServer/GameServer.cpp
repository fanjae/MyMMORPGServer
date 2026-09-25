#include "../ServerCore/SocketUtils.h"
#include "../ServerCore/Session.h"
#include "../ServerCore/NetAddress.h"
#include "../ServerCore/Listener.h"
#include "../ServerCore/IocpCore.h"
#include "../ServerCore/IocpEvent.h"
#include "../ServerCore/IocpWorker.h"
#include "../ServerCore/SessionManager.h"
#include "AuthTicketManager.h"
#include "GameSession.h"
#include "ServerSession.h"

#include <iostream>

int main()
{
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
    NetAddress gameAddress(L"127.0.0.1", 7777);
    NetAddress serverAddress(L"127.0.0.1", 7778);

    Listener gameListener;
    Listener serverListener;

    if (!gameListener.Start(gameAddress))
    {
        SocketUtils::Clear();
        return 1;
    }

    if (!serverListener.Start(serverAddress))
    {
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

    IocpWorker worker(iocp, sessionManager);

    worker.RegisterListener(gameListener,
        [&authTicketManager](SOCKET socket)
        {
            return std::make_unique<GameSession>(socket, authTicketManager);
        });

    worker.RegisterListener(serverListener,
        [&authTicketManager](SOCKET socket)
        {
            return std::make_unique<ServerSession>(socket, authTicketManager);
        });

    std::cout << "Waiting for data...\n";

    while (true)
    {
        // 네트워크 이벤트가 없어도 주기적으로 만료 티켓을 정리해야 하므로
        // IOCP를 무한 대기하지 않고 1초 timeout으로 dispatch
        if (!worker.Dispatch(1000))
        {
            gameListener.Close();
            serverListener.Close();
            SocketUtils::Clear();
            return 1;
        }

        sessionManager.Cleanup();
        authTicketManager.CleanupExpired();
    }

    gameListener.Close();
    serverListener.Close();
    SocketUtils::Clear();
    return 0;
}