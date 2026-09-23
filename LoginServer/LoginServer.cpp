#include "../ServerCore/SocketUtils.h"
#include "../ServerCore/NetAddress.h"
#include "../ServerCore/Listener.h"
#include "../ServerCore/IocpCore.h"
#include "../ServerCore/IocpWorker.h"
#include "../ServerCore/SessionManager.h"

#include "AccountRepository.h"
#include "AuthKeyGenerator.h"
#include "CharacterRepository.h"
#include "DatabaseConnection.h"
#include "GameServerClient.h"
#include "LoginSession.h"
#include "PasswordVerifier.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
    std::string GetEnvironmentVariable(const char* name)
    {
        char* value = nullptr;
        size_t length = 0;

        if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
        {
            std::cerr << "Environment variable is missing: " << name << '\n';
            return {};
        }

        std::string result(value);
        free(value);

        return result;
    }
}

int main()
{
    const std::string dbHost = GetEnvironmentVariable("DB_HOST");
    const std::string dbUser = GetEnvironmentVariable("DB_USER");
    const std::string dbPassword = GetEnvironmentVariable("DB_PASSWORD");
    const std::string dbName = GetEnvironmentVariable("DB_NAME");

    if (dbHost.empty() || dbUser.empty() || dbPassword.empty() || dbName.empty())
        return 1;

    DatabaseConnection database;

    if (!database.Connect(dbHost, dbUser, dbPassword, dbName))
        return 1;

    if (!database.TestConnection())
        return 1;

    std::cout << "Database Connected\n";

    AccountRepository accountRepository(*database.GetConnection());
    CharacterRepository characterRepository(*database.GetConnection());
    PasswordVerifier passwordVerifier;

    if (!SocketUtils::Init())
        return 1;

    IocpCore iocp;

    if (!iocp.IsValid())
    {
        SocketUtils::Clear();
        return 1;
    }

    NetAddress address(L"127.0.0.1", 7776);
    Listener listener;

    if (!listener.Start(address))
    {
        SocketUtils::Clear();
        return 1;
    }

    if (!iocp.Register(reinterpret_cast<HANDLE>(listener.GetSocket()), reinterpret_cast<ULONG_PTR>(&listener)))
    {
        listener.Close();
        SocketUtils::Clear();
        return 1;
    }

    if (!listener.PostAccept())
    {
        listener.Close();
        SocketUtils::Clear();
        return 1;
    }

    SessionManager sessionManager;
    GameServerClient gameServerClient;
    AuthKeyGenerator authKeyGenerator;
    IocpWorker worker(iocp, sessionManager);

    worker.RegisterListener(listener,
        [&gameServerClient, &authKeyGenerator, &accountRepository, &characterRepository, &passwordVerifier](SOCKET socket)
        {
            return std::make_unique<LoginSession>(socket, gameServerClient, authKeyGenerator, accountRepository, characterRepository, passwordVerifier);
        });

    std::cout << "Login Server Started\n";

    while (true)
    {
        if (!worker.Dispatch(INFINITE))
        {
            listener.Close();
            SocketUtils::Clear();
            return 1;
        }

        sessionManager.Cleanup();
    }

    listener.Close();
    SocketUtils::Clear();
    return 0;
}