#pragma once
#include "../Protocol/LoginPacket.h"
#include <atomic>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

class DatabaseConnection;

// 가입의 해시 계산·DB 저장은 전용 연결에서 처리하고 IOCP에서는 완료 여부만 확인한다.
class RegistrationService
{
public:
    RegistrationService(std::string host, std::string user, std::string password, std::string schema);
    ~RegistrationService();
    std::future<RegistrationResult> Submit(std::string id, std::string password, std::shared_ptr<std::atomic<bool>> canceled);
private:
    void Run();
    std::string _host, _user, _password, _schema;
    std::mutex _mutex;
    std::condition_variable _ready;
    std::queue<std::packaged_task<RegistrationResult(DatabaseConnection&)>> _jobs;
    bool _stopping = false;
    std::thread _worker;
};
