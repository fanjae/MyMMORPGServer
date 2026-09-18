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
    ULONG_PTR key = reinterpret_cast<ULONG_PTR>(&listener);
    _listeners[key] = ListenerContext{ &listener, std::move(sessionFactory) };
}

bool IocpWorker::Dispatch(DWORD timeoutMs)
{
    DWORD bytes = 0;
    ULONG_PTR key = 0;
    OVERLAPPED* overlapped = nullptr;
    bool ioSuccess = false;

    if (!_iocp.GetCompletion(bytes, key, overlapped, ioSuccess, timeoutMs))
        return false;

    if (overlapped == nullptr)
        return false;

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

            auto session = context.sessionFactory(acceptEvent->acceptSocket);
            if (session == nullptr)
                return false;

            acceptEvent->acceptSocket = INVALID_SOCKET;

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