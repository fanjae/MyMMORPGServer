#pragma once

#include <algorithm>
#include <memory>
#include <vector>


class Session;

class SessionManager
{
public:
    void Add(std::unique_ptr<Session> session);
    void Cleanup();

private:
    std::vector<std::unique_ptr<Session>> _sessions;
};