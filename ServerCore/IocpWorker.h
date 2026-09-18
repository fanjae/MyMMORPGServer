#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <windows.h>

class IocpCore;
class Listener;
class Session;
class SessionManager;

class IocpWorker
{
public:
    using SessionFactory = std::function<std::unique_ptr<Session>(SOCKET)>;

    IocpWorker(IocpCore& iocp, SessionManager& sessionManager);

    void RegisterListener(Listener& listener, SessionFactory sessionFactory);
    bool Dispatch(DWORD timeoutMs = INFINITE);

private:
    struct ListenerContext
    {
        Listener* listener = nullptr;
        SessionFactory sessionFactory;
    };

    IocpCore& _iocp;
    SessionManager& _sessionManager;
    std::unordered_map<ULONG_PTR, ListenerContext> _listeners;
};