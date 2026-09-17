#include "pch.h"
#include "IocpWorker.h"
#include "IocpCore.h"
#include "IocpEvent.h"
#include "Listener.h"
#include "Session.h"

#include <iostream>

IocpWorker::IocpWorker(IocpCore& iocp, Listener& listener) : _iocp(iocp), _listener(listener)
{
}

bool IocpWorker::Dispatch(DWORD timeoutMs)
{
    DWORD bytes = 0;
    ULONG_PTR key = 0;
    OVERLAPPED* overlapped = nullptr;

    if (!_iocp.GetCompletion(bytes, key, overlapped, timeoutMs))
        return false;

    if (overlapped == nullptr)
        return false;

    IocpEvent* event = reinterpret_cast<IocpEvent*>(overlapped);

    switch (event->type)
    {
        case IocpEventType::Accept:
        {
            AcceptEvent* acceptEvent = static_cast<AcceptEvent*>(event);

            if (!_listener.CompleteAccept(*acceptEvent))
                return false;

            std::cout << "Accept Event\n";
            break;
        }

        case IocpEventType::Recv:
        {
            Session* session = reinterpret_cast<Session*>(key);
            if (session == nullptr)
                return false;

            if (!session->OnRecv(bytes))
                return false;

            break;
        }

        case IocpEventType::Send:
            std::cout << "Send Event\n";
            break;

        default:
            return false;
    }

    return true;
}