#include "pch.h"
#include "DatabaseConnection.h"

#include <iostream>
#include <memory>

DatabaseConnection::DatabaseConnection() = default;

DatabaseConnection::~DatabaseConnection()
{
    Disconnect();
}

bool DatabaseConnection::Connect(const std::string& host, const std::string& user, const std::string& password, const std::string& database)
{
    try
    {
        sql::Driver* driver = sql::mysql::get_driver_instance();

        // DB 연결과 응답 대기가 서버 처리를 무기한 막지 않도록 초 단위로 제한한다.
        sql::ConnectOptionsMap options;
        options["hostName"] = host;
        options["userName"] = user;
        options["password"] = password;
        options["OPT_CONNECT_TIMEOUT"] = 3;
        options["OPT_READ_TIMEOUT"] = 3;
        options["OPT_WRITE_TIMEOUT"] = 3;
        options["OPT_CHARSET_NAME"] = std::string("utf8mb4");
        _connection.reset(driver->connect(options));
        _connection->setSchema(database);

        return true;
    }
    catch (const sql::SQLException& e)
    {
        std::cerr << "Database connection failed: " << e.what() << '\n';
        _connection.reset();
        return false;
    }
}

bool DatabaseConnection::TestConnection()
{
    if (!_connection)
        return false;

    try
    {
        std::unique_ptr<sql::Statement> statement(_connection->createStatement());
        std::unique_ptr<sql::ResultSet> result(statement->executeQuery("SELECT 1"));

        return result->next() && result->getInt(1) == 1;
    }
    catch (const sql::SQLException& e)
    {
        std::cerr << "Database test query failed: " << e.what() << '\n';
        return false;
    }
}

void DatabaseConnection::Disconnect()
{
    if (!_connection)
        return;

    try
    {
        _connection->close();
    }
    catch (const sql::SQLException&)
    {
    }

    _connection.reset();
}

sql::Connection* DatabaseConnection::GetConnection()
{
    return _connection.get();
}
