#include "RegistrationService.h"
#include "AccountRepository.h"
#include "PasswordVerifier.h"
#include "../Protocol/RegistrationCredentials.h"
#include "../ServerCore/DatabaseConnection.h"
#include <chrono>
#include <Windows.h>

RegistrationService::RegistrationService(std::string host, std::string user, std::string password, std::string schema)
    : _host(std::move(host)), _user(std::move(user)), _password(std::move(password)), _schema(std::move(schema)), _worker([this] { Run(); }) {}

RegistrationService::~RegistrationService()
{
    { std::lock_guard<std::mutex> lock(_mutex); _stopping = true; while (!_jobs.empty()) _jobs.pop(); }
    _ready.notify_one(); _worker.join();
}

std::future<RegistrationResult> RegistrationService::Submit(std::string id, std::string password, std::shared_ptr<std::atomic<bool>> canceled)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    std::packaged_task<RegistrationResult(DatabaseConnection&)> job(
        [this, id = std::move(id), password = std::move(password), canceled, deadline](DatabaseConnection& database) mutable
    {
        if (!IsValidRegistrationId(id)) return RegistrationResult::InvalidId;
        if (!IsValidRegistrationPassword(password)) return RegistrationResult::InvalidPassword;
        auto expired = [&] { return canceled->load() || std::chrono::steady_clock::now() >= deadline; };
        if (expired()) return RegistrationResult::Busy;
        if (!database.TestConnection())
        {
            if (!database.Connect(_host, _user, _password, _schema)) return RegistrationResult::ServerError;
            std::unique_ptr<sql::Statement> setting(database.GetConnection()->createStatement());
            setting->execute("SET SESSION innodb_lock_wait_timeout=3");
        }
        for (auto& value : id) if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
        AccountRepository accounts(*database.GetConnection());
        const auto existing = accounts.FindByLoginId(id);
        if (existing.status == RepositoryStatus::Success) return RegistrationResult::DuplicateId;
        if (existing.status == RepositoryStatus::DatabaseError) { database.Disconnect(); return RegistrationResult::ServerError; }
        std::string hash; const bool hashed = PasswordVerifier().CreateHash(password, hash);
        SecureZeroMemory(password.data(), password.size()); password.clear();
        if (!hashed) return RegistrationResult::ServerError;
        if (expired()) return RegistrationResult::Busy;
        return accounts.CreateWithCharacter(id, hash);
    });
    auto future = job.get_future();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_stopping || _jobs.size() >= 8) return {};
        _jobs.push(std::move(job));
    }
    _ready.notify_one(); return future;
}

void RegistrationService::Run()
{
    auto* driver = sql::mysql::get_driver_instance(); driver->threadInit();
    {
        DatabaseConnection database;
        while (true)
        {
            std::packaged_task<RegistrationResult(DatabaseConnection&)> job;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _ready.wait(lock, [this] { return _stopping || !_jobs.empty(); });
                if (_stopping) break;
                job = std::move(_jobs.front()); _jobs.pop();
            }
            job(database);
        }
    }
    driver->threadEnd();
}
