#pragma once

#include <windows.h>

class IocpCore;
class Listener;

class IocpWorker
{
public:
    IocpWorker(IocpCore& iocp, Listener& listener);

    bool Dispatch(DWORD timeoutMs = INFINITE);

private:
    IocpCore& _iocp;
    Listener& _listener;
};