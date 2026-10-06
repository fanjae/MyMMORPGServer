#include "pch.h"
#include "SessionManager.h"
#include "Session.h"

void SessionManager::Add(std::unique_ptr<Session> session)
{
    _sessions.push_back(std::move(session));
}

void SessionManager::Cleanup()
{
    // 브로드캐스트 중에는 종료를 예약하고 Map 순회가 끝난 뒤 Player를 제거한다.
    for (const auto& session : _sessions)
    {
        if (session->IsConnected())
            session->Update();
        if (session->IsCloseRequested())
            session->Close();
    }

    _sessions.erase(std::remove_if(_sessions.begin(), _sessions.end(),
            [](const std::unique_ptr<Session>& session)
            {
                return session->CanDestroy();
            }),
        _sessions.end());
}

size_t SessionManager::GetQueuedSendBytes() const
{
    size_t bytes = 0;
    for (const auto& session : _sessions)
        bytes += session->GetQueuedSendBytes();
    return bytes;
}
