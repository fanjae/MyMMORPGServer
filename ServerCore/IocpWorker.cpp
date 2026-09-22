#include "pch.h"
#include "IocpWorker.h"
#include "IocpCore.h"
#include "IocpEvent.h"
#include "Listener.h"
#include "Session.h"
#include "SessionManager.h"

#include <iostream>

IocpWorker::IocpWorker(IocpCore& iocp, SessionManager& sessionManager) : _iocp(iocp), _sessionManager(sessionManager)
{
}

void IocpWorker::RegisterListener(Listener& listener, SessionFactory sessionFactory)
{
    // Listener 주소를 listen socket의 IOCP Completion Key로 사용
    // 따라서 등록된 Listener는 pending Accept가 존재하는 동안 이동되거나 파괴되어서는 안 됨.
    ULONG_PTR key = reinterpret_cast<ULONG_PTR>(&listener);
    _listeners[key] = ListenerContext{ &listener, std::move(sessionFactory) };
}

bool IocpWorker::Dispatch(DWORD timeoutMs)
{
    DWORD bytes = 0;
    ULONG_PTR key = 0;
    OVERLAPPED* overlapped = nullptr;
    bool ioSuccess = false;
    bool timedOut = false;

    if (!_iocp.GetCompletion(bytes, key, overlapped, ioSuccess, timedOut, timeoutMs))
        return false;

    if (timedOut)
        return true;

    if (overlapped == nullptr)
        return false;

    // 모든 IocpEvent는 OVERLAPPED를 첫 번째 멤버로 두므로
    // IOCP가 돌려준 OVERLAPPED*에서 원래 Event 객체 주소를 복원할 수 있다.
    IocpEvent* event = reinterpret_cast<IocpEvent*>(overlapped);

    switch (event->type)
    {
        case IocpEventType::Accept:
        {
            auto it = _listeners.find(key);
            if (it == _listeners.end())
                return false;

            ListenerContext& context = it->second;
            AcceptEvent* acceptEvent = static_cast<AcceptEvent*>(event);

            if (!context.listener->CompleteAccept(*acceptEvent))
                return false;

            // AcceptEvent가 보관하던 accepted socket의 소유권을 새 Session으로 이전한다.
            // Session 생성 후에는 AcceptEvent가 해당 socket을 다시 닫지 않도록 비운다.

            auto session = context.sessionFactory(acceptEvent->acceptSocket);
            if (session == nullptr)
                return false;

            acceptEvent->acceptSocket = INVALID_SOCKET;

            // Session 주소를 socket의 Completion Key로 등록한다.
            // 이후 Recv/Send completion은 이 주소를 통해 해당 Session으로 dispatch된다.
            // pending I/O가 남아 있는 동안 Session 주소가 유효해야 한다.
            if (!_iocp.Register(reinterpret_cast<HANDLE>(session->GetSocket()), reinterpret_cast<ULONG_PTR>(session.get())))
                return false;

            if (!session->PostRecv())
                return false;

            _sessionManager.Add(std::move(session));

            if (!context.listener->PostAccept())
                return false;

            std::cout << "Session Created\n";

            break;
        }

        case IocpEventType::Recv:
        {
            Session* session = reinterpret_cast<Session*>(key);
            if (session == nullptr)
                return false;

            if (!session->Dispatch(event, bytes, ioSuccess))
            {
                session->Close();
                break;
            }

            break;
        }

        case IocpEventType::Send:
        {
            Session* session = reinterpret_cast<Session*>(key);
            if (session == nullptr)
                return false;

            if (!session->Dispatch(event, bytes, ioSuccess))
            {
                session->Close();
                break;
            }

            break;
        }

        default:
            return false;
    }

    return true;
}