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
        [](SOCKET socket)
        {
            return std::make_unique<GameSession>(socket);
        });

    worker.RegisterListener(serverListener,
        [&authTicketManager](SOCKET socket)
        {
            return std::make_unique<ServerSession>(socket, authTicketManager);
        });

    std::cout << "Waiting for data...\n";

    while (true)
    {
        if (!worker.Dispatch(INFINITE))
        {
            gameListener.Close();
            serverListener.Close();
            SocketUtils::Clear();
            return 1;
        }

        sessionManager.Cleanup();
    }

    gameListener.Close();
    serverListener.Close();
    SocketUtils::Clear();
    return 0;
}