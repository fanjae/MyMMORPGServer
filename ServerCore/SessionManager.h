#pragma once

#include <algorithm>
#include <memory>
#include <vector>


class Session;

// 활성 Session의 소유권을 보관한다.
// socket이 닫힌 뒤에도 pending IOCP completion이 남아 있을 수 있으므로 CanDestroy()가 true가 된 객체만 제거한다.
class SessionManager
{
public:
    void Add(std::unique_ptr<Session> session);
    void Cleanup();

private:
    std::vector<std::unique_ptr<Session>> _sessions;
};