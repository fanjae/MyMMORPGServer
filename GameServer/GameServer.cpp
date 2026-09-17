#include "../ServerCore/SocketUtils.h"
#include "../ServerCore/Session.h"
#include "../ServerCore/NetAddress.h"
#include "../ServerCore/Listener.h"
#include "../ServerCore/IocpCore.h"
#include "../ServerCore/IocpEvent.h"
#include "../ServerCore/IocpWorker.h"

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

    NetAddress address(L"127.0.0.1", 7777);
    Listener listener;

    if (!listener.Start(address))
    {
        SocketUtils::Clear();
        return 1;
    }

    if (!iocp.Register(reinterpret_cast<HANDLE>(listener.GetSocket()), 1))
    {
        listener.Close();
        SocketUtils::Clear();
        return 1;
    }

    // 실제 클라이언트 연결을 비동기 Accept로 대기
    AcceptEvent acceptEvent;

    if (!listener.PostAccept(acceptEvent))
    {
        listener.Close();
        SocketUtils::Clear();
        return 1;
    }

    IocpWorker worker(iocp, listener);

    if (!worker.Dispatch(INFINITE))
    {
        SocketUtils::Close(acceptEvent.acceptSocket);
        listener.Close();
        SocketUtils::Clear();
        return 1;
    }

    Session session(acceptEvent.acceptSocket);
    acceptEvent.acceptSocket = INVALID_SOCKET;

    if (!iocp.Register(reinterpret_cast<HANDLE>(session.GetSocket()), reinterpret_cast<ULONG_PTR>(&session)))
    {
        session.Close();
        listener.Close();
        SocketUtils::Clear();
        return 1;
    }

    std::cout << "Session Created\n";

    if (!session.PostRecv())
    {
        session.Close();
        listener.Close();
        SocketUtils::Clear();
        return 1;
    }

    std::cout << "Waiting for data...\n";

    while (session.IsConnected())
    {
        if (!worker.Dispatch(INFINITE))
        {
            session.Close();
            listener.Close();
            SocketUtils::Clear();
            return 1;
        }
    }

    listener.Close();
    SocketUtils::Clear();
    return 0;
}