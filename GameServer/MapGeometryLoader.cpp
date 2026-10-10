#include "MapGeometryLoader.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <unordered_set>

namespace
{
    constexpr size_t MaxObjectsPerMap = 4096;

    template<typename T>
    bool ReadNumber(const std::string& field, T& value)
    {
        auto result = std::from_chars(field.data(), field.data() + field.size(), value);
        return result.ec == std::errc{} && result.ptr == field.data() + field.size();
    }

    bool ReadRows(const std::filesystem::path& path, const char* header, size_t count, const std::function<bool(const std::vector<std::string>&)>& readRow, std::string& error)
    {
        std::ifstream file(path);
        std::string line;
        size_t lineNumber = 0;

        while (std::getline(file, line))
        {
            ++lineNumber;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            if (lineNumber == 1 && line == header)
                continue;

            std::vector<std::string> fields;
            std::istringstream row(line);
            std::string field;
            while (std::getline(row, field, ','))
                fields.push_back(field);

            if (lineNumber == 1 || line.empty() || line.back() == ',' || fields.size() != count || !readRow(fields))
            {
                error = "Invalid geometry data: " + path.string() + " line=" + std::to_string(lineNumber);
                return false;
            }
        }

        if (file.bad() || lineNumber == 0)
        {
            error = "Geometry data could not be read: " + path.string();
            return false;
        }

        return true;
    }
}

bool MapGeometryLoader::Validate(const MapGeometry& geometry, std::string& error)
{
    error.clear();
    const MapDefinition& map = geometry.map;
    const PlatformMovementSettings& movement = geometry.movement;
    auto fail = [&error](const char* message) { error = message; return false; };

    if (!map.IsValid() || movement.geometryVersion == 0 || movement.halfWidth == 0 || movement.halfWidth > 1000 || movement.halfHeight == 0 || movement.halfHeight > 1000)
        return fail("Invalid map or collision size");

    if (geometry.footholds.size() > MaxObjectsPerMap || geometry.colliders.size() > MaxObjectsPerMap)
        return fail("Too many geometry objects");

    if (movement.movementMode == MovementMode::Character3D)
    {
        if (!geometry.footholds.empty() || !geometry.colliders.empty() || movement.spawnFootholdId != 0)
            return fail("3D map contains 2D geometry");
        return true; // 3D 크기·spawn·박스 검증은 World3DLoader에서 함께 처리한다.
    }

    if (movement.movementMode == MovementMode::Free)
    {
        if (movement.spawnFootholdId != 0 || movement.horizontalSpeed != map.moveSpeed || movement.jumpSpeed != 0 || movement.gravity != 0 || movement.maxFallSpeed != 0 || !geometry.footholds.empty() || !geometry.colliders.empty())
            return fail("Free map contains platform movement data");

        return true;
    }

    if (movement.movementMode != MovementMode::Platformer || movement.horizontalSpeed == 0 || movement.horizontalSpeed > 10000 || movement.jumpSpeed == 0 || movement.jumpSpeed > 10000 || movement.gravity == 0 || movement.gravity > 10000 || movement.maxFallSpeed == 0 || movement.maxFallSpeed > 10000)
        return fail("Invalid platform movement settings");

    double halfWidth = movement.halfWidth;
    double halfHeight = movement.halfHeight;
    if (static_cast<double>(map.minX) + halfWidth >= static_cast<double>(map.maxX) - halfWidth || static_cast<double>(map.minY) + halfHeight >= static_cast<double>(map.maxY) - halfHeight)
        return fail("Collision size does not fit inside map bounds");

    if (map.spawnX - halfWidth < map.minX || map.spawnX + halfWidth > map.maxX || map.spawnY - halfHeight < map.minY || map.spawnY + halfHeight > map.maxY)
        return fail("Spawn collision area is outside map bounds");

    std::unordered_set<uint32_t> ids;
    std::vector<const FootholdDefinition*> ordered;
    for (const FootholdDefinition& foothold : geometry.footholds)
    {
        if (foothold.footholdId == 0 || !ids.insert(foothold.footholdId).second || foothold.x1 >= foothold.x2 || foothold.y1 != foothold.y2 || !map.Contains(foothold.x1, foothold.y1) || !map.Contains(foothold.x2, foothold.y2))
            return fail("Invalid or duplicate foothold");

        ordered.push_back(&foothold);
    }

    std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right)
    {
        return left->y1 != right->y1 ? left->y1 < right->y1 : left->x1 < right->x1;
    });

    for (size_t i = 1; i < ordered.size(); ++i)
    {
        if (ordered[i - 1]->y1 == ordered[i]->y1 && ordered[i - 1]->x2 > ordered[i]->x1)
            return fail("Overlapping footholds");
    }

    for (const FootholdDefinition& foothold : geometry.footholds)
    {
        const FootholdDefinition* previous = geometry.FindFoothold(foothold.prevId);
        const FootholdDefinition* next = geometry.FindFoothold(foothold.nextId);
        if (foothold.prevId != 0 && (previous == nullptr || previous->nextId != foothold.footholdId || previous->x2 != foothold.x1 || previous->y2 != foothold.y1))
            return fail("Invalid previous foothold connection");

        if (foothold.nextId != 0 && (next == nullptr || next->prevId != foothold.footholdId || foothold.x2 != next->x1 || foothold.y2 != next->y1))
            return fail("Invalid next foothold connection");
    }

    const FootholdDefinition* spawn = geometry.FindFoothold(movement.spawnFootholdId);
    if (spawn == nullptr || map.spawnX < spawn->x1 || map.spawnX > spawn->x2 || map.spawnY - halfHeight != spawn->y1)
        return fail("Spawn is not on the specified foothold");

    for (const FootholdDefinition& foothold : geometry.footholds)
    {
        if (foothold.footholdId < spawn->footholdId && map.spawnX >= foothold.x1 && map.spawnX <= foothold.x2 && map.spawnY - halfHeight == foothold.y1)
            return fail("Spawn foothold must use the smallest ID at a shared endpoint");
    }

    ids.clear();
    for (const MapCollisionDefinition& collider : geometry.colliders)
    {
        if (collider.colliderId == 0 || !ids.insert(collider.colliderId).second || collider.minX >= collider.maxX || collider.minY >= collider.maxY || !map.Contains(collider.minX, collider.minY) || !map.Contains(collider.maxX, collider.maxY))
            return fail("Invalid or duplicate collider");

        if (map.spawnX + halfWidth > collider.minX && map.spawnX - halfWidth < collider.maxX && map.spawnY + halfHeight > collider.minY && map.spawnY - halfHeight < collider.maxY)
            return fail("Spawn collision area overlaps a collider");

        for (const FootholdDefinition& foothold : geometry.footholds)
        {
            if (foothold.y1 > collider.minY && foothold.y1 < collider.maxY && foothold.x2 > collider.minX && foothold.x1 < collider.maxX)
                return fail("Foothold passes through a collider");
        }
    }

    return true;
}

bool MapGeometryLoader::Load(const std::string& directory, const std::unordered_map<uint32_t, MapDefinition>& maps, std::unordered_map<uint32_t, MapGeometry>& geometries, std::string& error)
{
    error.clear();
    std::unordered_map<uint32_t, MapGeometry> loaded;
    std::filesystem::path root(directory);
    if (maps.empty())
    {
        error = "No maps available for geometry loading";
        return false;
    }

    bool valid = ReadRows(root / "map_movement.csv", "mapId,geometryVersion,movementMode,spawnFootholdId,halfWidth,halfHeight,horizontalSpeed,jumpSpeed,gravity,maxFallSpeed", 10, [&](const auto& fields)
    {
        uint32_t mapId = 0;
        MapGeometry geometry;
        PlatformMovementSettings& movement = geometry.movement;
        if (!ReadNumber(fields[0], mapId) || maps.find(mapId) == maps.end() || loaded.find(mapId) != loaded.end() || !ReadNumber(fields[1], movement.geometryVersion) || !ReadNumber(fields[3], movement.spawnFootholdId) || !ReadNumber(fields[4], movement.halfWidth) || !ReadNumber(fields[5], movement.halfHeight) || !ReadNumber(fields[6], movement.horizontalSpeed) || !ReadNumber(fields[7], movement.jumpSpeed) || !ReadNumber(fields[8], movement.gravity) || !ReadNumber(fields[9], movement.maxFallSpeed))
            return false;

        if (fields[2] != "Free" && fields[2] != "Platformer" && fields[2] != "Character3D")
            return false;

        geometry.map = maps.at(mapId);
        movement.movementMode = fields[2] == "Free" ? MovementMode::Free :
            fields[2] == "Platformer" ? MovementMode::Platformer : MovementMode::Character3D;
        loaded.emplace(mapId, std::move(geometry));
        return true;
    }, error);

    if (!valid)
        return false;

    valid = ReadRows(root / "footholds.csv", "mapId,footholdId,x1,y1,x2,y2,prevId,nextId", 8, [&](const auto& fields)
    {
        uint32_t mapId = 0;
        FootholdDefinition foothold;
        if (!ReadNumber(fields[0], mapId) || loaded.find(mapId) == loaded.end() || !ReadNumber(fields[1], foothold.footholdId) || !ReadNumber(fields[2], foothold.x1) || !ReadNumber(fields[3], foothold.y1) || !ReadNumber(fields[4], foothold.x2) || !ReadNumber(fields[5], foothold.y2) || !ReadNumber(fields[6], foothold.prevId) || !ReadNumber(fields[7], foothold.nextId))
            return false;

        auto& footholds = loaded.at(mapId).footholds;
        if (footholds.size() >= MaxObjectsPerMap)
            return false;

        footholds.push_back(foothold);
        return true;
    }, error);

    if (!valid)
        return false;

    valid = ReadRows(root / "colliders.csv", "mapId,colliderId,minX,minY,maxX,maxY", 6, [&](const auto& fields)
    {
        uint32_t mapId = 0;
        MapCollisionDefinition collider;
        if (!ReadNumber(fields[0], mapId) || loaded.find(mapId) == loaded.end() || !ReadNumber(fields[1], collider.colliderId) || !ReadNumber(fields[2], collider.minX) || !ReadNumber(fields[3], collider.minY) || !ReadNumber(fields[4], collider.maxX) || !ReadNumber(fields[5], collider.maxY))
            return false;

        auto& colliders = loaded.at(mapId).colliders;
        if (colliders.size() >= MaxObjectsPerMap)
            return false;

        colliders.push_back(collider);
        return true;
    }, error);

    if (!valid)
        return false;

    if (loaded.size() != maps.size())
    {
        error = "Movement settings are missing for a map";
        return false;
    }

    for (auto& entry : loaded)
    {
        MapGeometry& geometry = entry.second;
        if (!Validate(geometry, error))
        {
            error = "Invalid geometry map=" + std::to_string(entry.first) + ": " + error;
            return false;
        }

        std::sort(geometry.footholds.begin(), geometry.footholds.end(), [](const auto& left, const auto& right) { return left.footholdId < right.footholdId; });
        std::sort(geometry.colliders.begin(), geometry.colliders.end(), [](const auto& left, const auto& right) { return left.colliderId < right.colliderId; });
    }

    // 모든 파일을 검증한 뒤 적용해 실패 시 기존 지형 데이터가 유지되도록 한다.
    geometries.swap(loaded);
    return true;
}
