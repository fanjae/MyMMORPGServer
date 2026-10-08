#include "MapManager.h"
#include "Map.h"
#include "MapGeometryLoader.h"

#include <array>
#include <charconv>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

MapManager::~MapManager() = default;

bool MapManager::LoadMaps(const std::string& path)
{
    if (!_maps.empty())
        return false;

    std::ifstream file(path);
    std::string line;
    if (!std::getline(file, line))
    {
        std::cerr << "Map data could not be read: " << path << '\n';
        return false;
    }

    if (!line.empty() && line.back() == '\r')
        line.pop_back();

    if (line != "mapId,spawnX,spawnY,minX,maxX,minY,maxY,moveSpeed,moveBurst")
    {
        std::cerr << "Invalid map data header: " << path << '\n';
        return false;
    }

    std::unordered_map<uint32_t, std::unique_ptr<Map>> maps;
    uint32_t lineNumber = 1;

    while (std::getline(file, line))
    {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        std::istringstream row(line);
        std::array<int64_t, 9> values{};
        bool valid = !line.empty() && line.back() != ',';

        for (int64_t& value : values)
        {
            std::string field;
            if (!std::getline(row, field, ','))
            {
                valid = false;
                break;
            }

            auto result = std::from_chars(field.data(), field.data() + field.size(), value);
            if (result.ec != std::errc{} || result.ptr != field.data() + field.size())
                valid = false;
        }

        std::string extra;
        if (std::getline(row, extra, ','))
            valid = false;

        for (size_t i = 1; i <= 6; ++i)
        {
            if (values[i] < (std::numeric_limits<int32_t>::min)() || values[i] > (std::numeric_limits<int32_t>::max)())
                valid = false;
        }

        for (size_t i : { 0u, 7u, 8u })
        {
            if (values[i] <= 0 || values[i] > (std::numeric_limits<uint32_t>::max)())
                valid = false;
        }

        MapDefinition definition;
        if (valid)
        {
            definition.mapId = static_cast<uint32_t>(values[0]);
            definition.spawnX = static_cast<int32_t>(values[1]);
            definition.spawnY = static_cast<int32_t>(values[2]);
            definition.minX = static_cast<int32_t>(values[3]);
            definition.maxX = static_cast<int32_t>(values[4]);
            definition.minY = static_cast<int32_t>(values[5]);
            definition.maxY = static_cast<int32_t>(values[6]);
            definition.moveSpeed = static_cast<uint32_t>(values[7]);
            definition.moveBurst = static_cast<uint32_t>(values[8]);
            valid = definition.IsValid() && maps.find(definition.mapId) == maps.end();
        }

        if (!valid)
        {
            std::cerr << "Invalid map data: " << path << " line=" << lineNumber << '\n';
            return false;
        }

        maps.emplace(definition.mapId, std::make_unique<Map>(definition));
    }

    if (file.bad() || maps.empty())
    {
        std::cerr << "Map data is empty or unreadable: " << path << '\n';
        return false;
    }

    // 모든 행을 검증한 뒤 한 번에 등록해 일부 Map만 로딩되는 상태를 피한다.
    _maps.swap(maps);
    return true;
}

bool MapManager::LoadGeometry(const std::string& directory)
{
    if (_maps.empty() || !_geometries.empty())
        return false;

    std::unordered_map<uint32_t, MapDefinition> definitions;
    for (const auto& entry : _maps)
        definitions.emplace(entry.first, entry.second->GetDefinition());

    std::string error;
    if (!MapGeometryLoader::Load(directory, definitions, _geometries, error))
    {
        std::cerr << error << '\n';
        return false;
    }

    for (const auto& entry : _geometries)
        _maps.at(entry.first)->SetGeometry(entry.second);

    return true;
}

const MapGeometry* MapManager::FindGeometry(uint32_t mapId) const
{
    auto it = _geometries.find(mapId);
    return it == _geometries.end() ? nullptr : &it->second;
}

Map* MapManager::FindMap(uint32_t mapId) const
{
    auto it = _maps.find(mapId);

    if (it == _maps.end())
        return nullptr;

    return it->second.get();
}

void MapManager::Advance()
{
    auto now = std::chrono::steady_clock::now();
    uint32_t count = 0;
    while (now >= _nextTick && count < 5)
    {
        auto tickStart = std::chrono::steady_clock::now();
        auto lag = std::chrono::duration_cast<std::chrono::milliseconds>(tickStart - _nextTick).count();
        _maxTickLagMilliseconds = (std::max)(_maxTickLagMilliseconds, static_cast<int64_t>(lag));
        ++_tick;
        for (const auto& entry : _maps)
            entry.second->Tick(_tick, tickStart);

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - tickStart).count();
        _maxTickMicroseconds = (std::max)(_maxTickMicroseconds, static_cast<int64_t>(duration));

        _nextTick += std::chrono::milliseconds(20);
        ++count;
    }

    // 긴 지연을 한 번에 재생하지 않고 처리량과 입력 유효 시간을 제한한다.
    if (now >= _nextTick)
    {
        std::cerr << "Map tick catch-up limit reached\n";
        ++_catchUpLimits;
        _nextTick = now + std::chrono::milliseconds(20);
    }
}

void MapManager::LogMetrics(size_t sessions, size_t queuedBytes)
{
    size_t players = 0;
    for (const auto& entry : _maps)
        players += entry.second->GetPlayerCount();
    // 일정 구간의 최대 tick 처리 시간과 누적 송신량을 함께 기록해 부하를 비교한다.
    std::cout << "Server metrics: sessions=" << sessions << " players=" << players
        << " queuedBytes=" << queuedBytes << " maxTickUs=" << _maxTickMicroseconds
        << " maxTickLagMs=" << _maxTickLagMilliseconds << " catchUpLimits=" << _catchUpLimits << '\n';
    _maxTickMicroseconds = 0;
    _maxTickLagMilliseconds = 0;
    _catchUpLimits = 0;
}

uint32_t MapManager::GetWaitMilliseconds() const
{
    auto remaining = _nextTick - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::steady_clock::duration::zero())
        return 0;

    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count() + 1);
}
