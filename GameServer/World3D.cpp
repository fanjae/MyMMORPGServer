#include "World3D.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <unordered_set>

namespace
{
    bool Rows(const std::filesystem::path& path, const char* header, size_t count,
        const std::function<bool(const std::vector<int64_t>&)>& read, std::string& error)
    {
        std::ifstream file(path);
        std::string line;
        if (!std::getline(file, line)) { error = "Missing 3D data: " + path.string(); return false; }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line != header) { error = "Invalid 3D header: " + path.string(); return false; }
        size_t row = 1;
        while (std::getline(file, line))
        {
            ++row;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::vector<int64_t> values;
            std::istringstream fields(line);
            std::string field;
            bool valid = !line.empty() && line.back() != ',';
            while (std::getline(fields, field, ','))
            {
                int64_t value = 0;
                auto result = std::from_chars(field.data(), field.data() + field.size(), value);
                valid &= result.ec == std::errc{} && result.ptr == field.data() + field.size() && value >= INT32_MIN && value <= INT32_MAX;
                values.push_back(value);
            }
            if (!valid || values.size() != count || !read(values))
            { error = "Invalid 3D data: " + path.string() + " row=" + std::to_string(row); return false; }
        }
        return !file.bad();
    }
    bool Overlap(int64_t center, int64_t radius, int32_t minimum, int32_t maximum)
    {
        return center + radius > static_cast<int64_t>(minimum) * 1000 && center - radius < static_cast<int64_t>(maximum) * 1000;
    }
}

uint32_t World3DDefinition::SpawnSupport() const
{
    for (const auto& box : boxes)
        if (settings.spawnY == box.maxY && settings.spawnX >= box.minX && settings.spawnX <= box.maxX &&
            settings.spawnZ >= box.minZ && settings.spawnZ <= box.maxZ)
            return box.id;
    return 0;
}

bool World3DDefinition::ValidateAction(const MovementAction3D& a) const
{
    const auto& s = settings;
    auto inside = [](int64_t value, int64_t minimum, int64_t maximum) { return value >= minimum * 1000 && value <= maximum * 1000; };
    if (a.grounded > 1 || a.kind > MovementActionKind::Fall || a.yaw >= 36000 ||
        static_cast<int64_t>(a.inputX) * a.inputX + static_cast<int64_t>(a.inputZ) * a.inputZ > 1000000 ||
        !inside(a.x, static_cast<int64_t>(s.minX) + s.radius, static_cast<int64_t>(s.maxX) - s.radius) ||
        !inside(a.z, static_cast<int64_t>(s.minZ) + s.radius, static_cast<int64_t>(s.maxZ) - s.radius) ||
        !inside(a.y, s.minY, static_cast<int64_t>(s.maxY) - s.height) ||
        !inside(a.velocityX, -static_cast<int64_t>(s.speed), s.speed) ||
        !inside(a.velocityZ, -static_cast<int64_t>(s.speed), s.speed) ||
        !inside(a.velocityY, -static_cast<int64_t>(s.maxFallSpeed), s.jumpSpeed)) return false;
    int64_t speed = static_cast<int64_t>(s.speed) * 1000;
    if (a.velocityX * a.velocityX + a.velocityZ * a.velocityZ > speed * speed + 2 * speed + 1) return false;
    if (a.grounded)
    {
        bool supported = false;
        for (const auto& box : boxes)
            if (box.id == a.supportId && a.y == static_cast<int64_t>(box.maxY) * 1000 && a.velocityY == 0 &&
                Overlap(a.x, static_cast<int64_t>(s.radius) * 1000, box.minX, box.maxX) &&
                Overlap(a.z, static_cast<int64_t>(s.radius) * 1000, box.minZ, box.maxZ)) supported = true;
        if (!supported) return false;
    }
    else if (a.supportId != 0) return false;
    if (a.kind == MovementActionKind::Respawn &&
        (a.x != static_cast<int64_t>(s.spawnX) * 1000 || a.y != static_cast<int64_t>(s.spawnY) * 1000 ||
            a.z != static_cast<int64_t>(s.spawnZ) * 1000 || !a.grounded || a.supportId != SpawnSupport())) return false;
    return true;
}

bool World3DLoader::Load(const std::string& directory, const std::unordered_map<uint32_t, MapDefinition>& maps,
    std::unordered_map<uint32_t, World3DDefinition>& worlds, std::string& error)
{
    if (maps.empty()) { worlds.clear(); return true; }
    std::unordered_map<uint32_t, World3DDefinition> loaded;
    auto root = std::filesystem::path(directory);
    if (!Rows(root / "worlds3d.csv", "mapId,version,minZ,maxZ,spawnZ,radius,height,speed,jumpSpeed,gravity,maxFallSpeed", 11,
        [&](const auto& v)
        {
            uint32_t id = static_cast<uint32_t>(v[0]);
            auto map = maps.find(id);
            if (map == maps.end() || loaded.count(id) || v[1] <= 0 || v[2] >= v[3]) return false;
            for (size_t i = 5; i < 11; ++i) if (v[i] <= 0 || v[i] > 10000) return false;
            World3DDefinition world;
            auto& s = world.settings;
            s.mapId = id; s.version = static_cast<uint32_t>(v[1]);
            s.minX = map->second.minX; s.maxX = map->second.maxX; s.minY = map->second.minY; s.maxY = map->second.maxY;
            s.minZ = static_cast<int32_t>(v[2]); s.maxZ = static_cast<int32_t>(v[3]);
            s.spawnX = map->second.spawnX; s.spawnY = map->second.spawnY; s.spawnZ = static_cast<int32_t>(v[4]);
            s.radius = static_cast<uint32_t>(v[5]); s.height = static_cast<uint32_t>(v[6]); s.speed = static_cast<uint32_t>(v[7]);
            s.jumpSpeed = static_cast<uint32_t>(v[8]); s.gravity = static_cast<uint32_t>(v[9]); s.maxFallSpeed = static_cast<uint32_t>(v[10]);
            loaded.emplace(id, std::move(world)); return true;
        }, error)) return false;
    if (loaded.size() != maps.size()) { error = "Missing 3D world settings"; return false; }
    if (!Rows(root / "boxes3d.csv", "mapId,id,minX,minY,minZ,maxX,maxY,maxZ", 8, [&](const auto& v)
        {
            auto found = loaded.find(static_cast<uint32_t>(v[0]));
            if (found == loaded.end() || v[1] <= 0 || found->second.boxes.size() >= 4096 || v[2] >= v[5] || v[3] >= v[6] || v[4] >= v[7]) return false;
            for (const auto& box : found->second.boxes) if (box.id == v[1]) return false;
            const auto& s = found->second.settings;
            if (v[2] < s.minX || v[3] < s.minY || v[4] < s.minZ || v[5] > s.maxX || v[6] > s.maxY || v[7] > s.maxZ) return false;
            found->second.boxes.push_back({s.mapId, 0, static_cast<uint32_t>(v[1]), static_cast<int32_t>(v[2]), static_cast<int32_t>(v[3]),
                static_cast<int32_t>(v[4]), static_cast<int32_t>(v[5]), static_cast<int32_t>(v[6]), static_cast<int32_t>(v[7])}); return true;
        }, error)) return false;
    for (auto& entry : loaded)
    {
        auto& world = entry.second;
        auto& s = world.settings;
        std::sort(world.boxes.begin(), world.boxes.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        s.boxCount = static_cast<uint16_t>(world.boxes.size());
        MovementAction3D spawn;
        spawn.x = static_cast<int64_t>(s.spawnX) * 1000; spawn.y = static_cast<int64_t>(s.spawnY) * 1000;
        spawn.z = static_cast<int64_t>(s.spawnZ) * 1000; spawn.supportId = world.SpawnSupport();
        if (s.radius > 1000 || s.height > 2000 || !world.ValidateAction(spawn)) { error = "Invalid 3D spawn"; return false; }
        for (const auto& box : world.boxes)
            if (Overlap(spawn.x, static_cast<int64_t>(s.radius) * 1000, box.minX, box.maxX) &&
                Overlap(spawn.z, static_cast<int64_t>(s.radius) * 1000, box.minZ, box.maxZ) &&
                s.spawnY < box.maxY && static_cast<int64_t>(s.spawnY) + s.height > box.minY)
            { error = "3D spawn overlaps solid geometry"; return false; }
    }
    // 서버는 지형의 기준 상태만 검증하며 이동·충돌·착지 물리를 실행하지 않는다.
    worlds.swap(loaded); return true;
}
