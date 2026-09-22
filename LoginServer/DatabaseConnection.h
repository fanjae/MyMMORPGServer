#pragma once

#include <mysql/jdbc.h>

#include <memory>
#include <string>
class DatabaseConnection
{
public:
    DatabaseConnection();
    ~DatabaseConnection();

    bool Connect(const std::string& host, const std::string& user, const std::string& password, const std::string& database);
    bool TestConnection();
    void Disconnect();

    sql::Connection* GetConnection();

private:
    std::unique_ptr<sql::Connection> _connection;
};