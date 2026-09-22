#include "DatabaseConnection.h"

#include <mysql/jdbc.h>

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

        _connection.reset(driver->connect(host, user, password));
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