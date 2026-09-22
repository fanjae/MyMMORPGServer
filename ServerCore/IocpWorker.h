#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <windows.h>

class IocpCore;
class Listener;
class Session;
class SessionManager;

// IOCP completion을 가져와 이벤트 종류에 따라 Listener 또는 Session으로 전달
// Listener는 Accept를 처리하고, 생성된 Session은 SessionManager가 수명을 관리
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

    // Completion Key(Listener 주소)별 Accept 처리 정보.
    std::unordered_map<ULONG_PTR, ListenerContext> _listeners;
};