#include "../ServerCore/SocketUtils.h"
#include "../ServerCore/NetAddress.h"
#include "../ServerCore/Listener.h"
#include "../ServerCore/IocpCore.h"
#include "../ServerCore/IocpWorker.h"
#include "../ServerCore/SessionManager.h"

#include "LoginSession.h"

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

    NetAddress address(L"127.0.0.1", 7776);
    Listener listener;

    if (!listener.Start(address))
    {
        SocketUtils::Clear();
        return 1;
    }

    if (!iocp.Register(reinterpret_cast<HANDLE>(listener.GetSocket()), reinterpret_cast<ULONG_PTR>(&listener)))
    {
        listener.Close();
        SocketUtils::Clear();
        return 1;
    }

    if (!listener.PostAccept())
    {
        listener.Close();
        SocketUtils::Clear();
        return 1;
    }

    SessionManager sessionManager;

    IocpWorker worker(iocp, sessionManager);

    worker.RegisterListener(listener,
        [](SOCKET socket)
        {
            return std::make_unique<LoginSession>(socket);
        });

    std::cout << "Login Server Started\n";

    while (true)
    {
        if (!worker.Dispatch(INFINITE))
        {
            listener.Close();
            SocketUtils::Clear();
            return 1;
        }

        sessionManager.Cleanup();
    }

    listener.Close();
    SocketUtils::Clear();
    return 0;
}