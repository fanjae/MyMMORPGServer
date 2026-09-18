#include "pch.h"
#include "SessionManager.h"
#include "Session.h"

void SessionManager::Add(std::unique_ptr<Session> session)
{
    _sessions.push_back(std::move(session));
}

void SessionManager::Cleanup()
{
    _sessions.erase(std::remove_if(_sessions.begin(), _sessions.end(),
            [](const std::unique_ptr<Session>& session)
            {
                return session->CanDestroy();
            }),
        _sessions.end());
}