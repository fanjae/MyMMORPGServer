#include "../../GameServer/MapGeometryLoader.h"
#include "../../GameServer/MovementSimulation.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const std::string& message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void Near(double actual, double expected, const char* message)
    {
        Check(std::abs(actual - expected) < 0.00001, message);
    }

    void Run(const char* name, const std::function<void()>& test)
    {
        try
        {
            test();
            std::cout << "[PASS] " << name << '\n';
        }
        catch (const std::exception& exception)
        {
            throw std::runtime_error(std::string(name) + ": " + exception.what());
        }
    }

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        Check(file.is_open(), "Fixture file missing");
        std::ostringstream text;
        text << file.rdbuf();
        return text.str();
    }

    class Fixture
    {
    public:
        explicit Fixture(const std::filesystem::path& source)
        {
            _path = std::filesystem::temp_directory_path() / ("MyMMORPGGeometry-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            Check(std::filesystem::create_directory(_path), "Fixture directory creation failed");
            for (const char* name : { "map_movement.csv", "footholds.csv", "colliders.csv" })
                _files.emplace(name, ReadFile(source / name));

            Restore();
        }

        ~Fixture()
        {
            std::error_code error;
            for (const auto& entry : _files)
                std::filesystem::remove(_path / entry.first, error);

            std::filesystem::remove(_path, error);
        }

        void Restore() const
        {
            for (const auto& entry : _files)
                Save(entry.first, entry.second);
        }

        void Save(const std::string& name, const std::string& text) const
        {
            std::ofstream file(_path / name, std::ios::binary | std::ios::trunc);
            file << text;
            Check(file.good(), "Fixture write failed");
        }

        void Replace(const std::string& name, const std::string& before, const std::string& after) const
        {
            std::string text = _files.at(name);
            size_t position = text.find(before);
            Check(position != std::string::npos, "Fixture replacement did not match");
            text.replace(position, before.size(), after);
            Save(name, text);
        }

        std::string Path() const { return _path.string(); }

    private:
        std::filesystem::path _path;
        std::unordered_map<std::string, std::string> _files;
    };
}

int main(int argc, char* argv[])
{
    try
    {
        std::filesystem::path source = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::absolute(argv[0]).parent_path() / "data";
        std::unordered_map<uint32_t, MapDefinition> maps;
        maps.emplace(100000000, MapDefinition{ 100000000, 0, 0, -400, 400, -200, 200, 80, 12 });
        maps.emplace(100000001, MapDefinition{ 100000001, 100, 50, -300, 500, -150, 250, 80, 12 });
        maps.emplace(100000002, MapDefinition{ 100000002, 0, 0, -400, 400, -200, 400, 80, 12 });
        maps.emplace(100000003, MapDefinition{ 100000003, -200, 0, -400, 400, -200, 400, 80, 12 });
        std::unordered_map<uint32_t, MapGeometry> geometries;
        std::string error;

        Run("Load sample geometry and Free mode", [&]
        {
            Check(MapGeometryLoader::Load(source.string(), maps, geometries, error), error);
            Check(geometries.size() == 4 && geometries.at(100000000).footholds.size() == 5 && geometries.at(100000000).colliders.size() == 1, "Sample object counts");
            Check(geometries.at(100000001).movement.movementMode == MovementMode::Free, "Free mode changed");
        });

        Fixture fixture(source);
        struct InvalidCase
        {
            const char* name;
            const char* file;
            const char* before;
            const char* after;
        };

        std::vector<InvalidCase> cases
        {
            { "Duplicate movement map", "map_movement.csv", "100000000,1,Platformer,1,6,6,80,240,480,320", "100000000,1,Platformer,1,6,6,80,240,480,320\n100000000,1,Platformer,1,6,6,80,240,480,320" },
            { "Unknown map", "map_movement.csv", "100000000,1,Platformer", "100000099,1,Platformer" },
            { "Unknown mode", "map_movement.csv", "Platformer", "Unknown" },
            { "Invalid header", "map_movement.csv", "mapId,geometryVersion", "map,geometryVersion" },
            { "Extra column", "map_movement.csv", "80,240,480,320", "80,240,480,320,1" },
            { "Trailing comma", "map_movement.csv", "80,240,480,320", "80,240,480,320," },
            { "Zero gravity", "map_movement.csv", "80,240,480,320", "80,240,0,320" },
            { "Unsigned overflow", "map_movement.csv", "100000000,1,Platformer", "100000000,4294967296,Platformer" },
            { "Zero collision size", "map_movement.csv", "Platformer,1,6,6", "Platformer,1,0,6" },
            { "Collision size outside map", "map_movement.csv", "Platformer,1,6,6", "Platformer,1,400,6" },
            { "Missing spawn foothold", "map_movement.csv", "Platformer,1,6,6", "Platformer,9999,6,6" },
            { "Spawn foot height mismatch", "map_movement.csv", "Platformer,1,6,6", "Platformer,1,6,7" },
            { "Shared endpoint spawn ID", "map_movement.csv", "Platformer,1,6,6", "Platformer,2,6,6" },
            { "Duplicate foothold", "footholds.csv", "100000000,3,-140,34,-20,34,0,0", "100000000,3,-140,34,-20,34,0,0\n100000000,3,-140,34,-20,34,0,0" },
            { "Negative foothold ID", "footholds.csv", "100000000,3,-140", "100000000,-3,-140" },
            { "Coordinate overflow", "footholds.csv", "-380,-6", "-2147483649,-6" },
            { "Sloped foothold", "footholds.csv", "-140,34,-20,34", "-140,34,-20,35" },
            { "Outside map foothold", "footholds.csv", "-380,-6", "-401,-6" },
            { "Missing neighbor", "footholds.csv", "0,-6,0,2", "0,-6,0,9999" },
            { "Non reciprocal neighbor", "footholds.csv", "380,-6,1,0", "380,-6,0,0" },
            { "Disconnected endpoints", "footholds.csv", "100000000,2,0,-6", "100000000,2,1,-6" },
            { "Overlapping footholds", "footholds.csv", "100000000,2,0,-6", "100000000,2,-1,-6" },
            { "Duplicate collider", "colliders.csv", "100000000,1,220,-6,244,80", "100000000,1,220,-6,244,80\n100000000,1,220,-6,244,80" },
            { "Invalid collider bounds", "colliders.csv", "220,-6,244,80", "244,-6,220,80" },
            { "Spawn inside collider", "colliders.csv", "220,-6,244,80", "-5,-5,5,5" },
            { "Foothold through collider", "colliders.csv", "220,-6,244,80", "-100,0,-80,60" },
            { "Geometry in Free map", "footholds.csv", "100000000,3,-140,34,-20,34,0,0", "100000001,3,-140,34,-20,34,0,0" }
        };

        for (const InvalidCase& invalid : cases)
        {
            Run(invalid.name, [&]
            {
                fixture.Restore();
                fixture.Replace(invalid.file, invalid.before, invalid.after);
                auto preserved = geometries;
                preserved.at(100000000).movement.geometryVersion = 77;
                Check(!MapGeometryLoader::Load(fixture.Path(), maps, preserved, error) && !error.empty(), "Invalid data accepted");
                Check(preserved.size() == 4 && preserved.at(100000000).movement.geometryVersion == 77 && preserved.at(100000000).footholds.size() == 5, "Failed load replaced existing geometry");
            });
        }

        Run("Missing movement settings", [&]
        {
            fixture.Restore();
            fixture.Save("map_movement.csv", "mapId,geometryVersion,movementMode,spawnFootholdId,halfWidth,halfHeight,horizontalSpeed,jumpSpeed,gravity,maxFallSpeed\n100000000,1,Platformer,1,6,6,80,240,480,320\n");
            auto preserved = geometries;
            Check(!MapGeometryLoader::Load(fixture.Path(), maps, preserved, error) && error == "Movement settings are missing for a map", "Missing map settings not detected");
            Check(preserved.size() == 4, "Missing settings cleared existing geometry");
        });

        Run("Missing file preserves geometry", [&]
        {
            fixture.Restore();
            std::filesystem::remove(std::filesystem::path(fixture.Path()) / "colliders.csv");
            Check(!MapGeometryLoader::Load(fixture.Path(), maps, geometries, error), "Missing file accepted");
            Check(geometries.at(100000000).footholds.size() == 5, "Missing file cleared existing data");
        });

        Run("Foothold IDs are local to a map", [&]
        {
            fixture.Restore();
            fixture.Replace("map_movement.csv", "100000001,1,Free,0,6,6,80,0,0,0", "100000001,1,Platformer,1,6,6,80,240,480,320");
            std::ofstream file(std::filesystem::path(fixture.Path()) / "footholds.csv", std::ios::app);
            file << "100000001,1,-280,44,480,44,0,0\n";
            file.close();
            auto loaded = geometries;
            Check(MapGeometryLoader::Load(fixture.Path(), maps, loaded, error), error);
            Check(loaded.at(100000000).FindFoothold(1) != nullptr && loaded.at(100000001).FindFoothold(1) != nullptr, "Map local ID rejected");
        });

        Run("Header only collider file is valid", [&]
        {
            fixture.Restore();
            fixture.Save("colliders.csv", "mapId,colliderId,minX,minY,maxX,maxY\n");
            auto loaded = geometries;
            Check(MapGeometryLoader::Load(fixture.Path(), maps, loaded, error), error);
            Check(loaded.at(100000000).colliders.empty(), "Empty collider file was not applied");
        });

        const MapGeometry& sample = geometries.at(100000000);
        if (argc > 2)
        {
            std::ofstream trace(argv[2]);
            Check(trace.is_open(), "Trace file could not be created");
            trace << "case,tick,horizontal,jump,x,y,vx,vy,foothold,grounded\n" << std::setprecision(17);
            MovementSimulation simulation(sample);
            for (int scenario = 0; scenario < 4; ++scenario)
            {
                PlatformMovementState state;
                simulation.Reset(state);
                for (int tick = 0; tick < 500; ++tick)
                {
                    PlatformMovementInput input;
                    input.horizontal = scenario == 1 || scenario == 2 ? -1 : 1;
                    input.jumpHeld = scenario == 1 ? tick < 5 : scenario == 3 && tick % 70 < 5;
                    SimulationResult result = simulation.Step(state, input);
                    Check(result == SimulationResult::Success || result == SimulationResult::Respawned, "Trace simulation failed");
                    trace << scenario << ',' << tick << ',' << static_cast<int>(input.horizontal) << ',' << input.jumpHeld << ',' << state.x << ',' << state.y << ',' << state.velocityX << ',' << state.velocityY << ',' << state.footholdId << ',' << state.grounded << '\n';
                }
            }

            Check(trace.good(), "Trace file write failed");
        }
        Run("Object limit and invalid simulation geometry", [&]
        {
            MapGeometry geometry = sample;
            geometry.colliders.resize(4097);
            Check(!MapGeometryLoader::Validate(geometry, error) && error == "Too many geometry objects", "Object limit not checked");
            geometry = sample;
            geometry.movement.gravity = 0;
            bool rejected = false;
            try
            {
                MovementSimulation simulation(geometry);
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }

            Check(rejected, "Simulation accepted invalid geometry");
        });
        Run("Ground movement, endpoint connection and stop", [&]
        {
            MovementSimulation simulation(sample);
            PlatformMovementState state;
            Check(simulation.Reset(state), "Reset failed");
            for (int i = 0; i < 25; ++i)
                Check(simulation.Step(state, { 1, false }) == SimulationResult::Success, "Ground step failed");

            Near(state.x, 40.0, "Wrong horizontal speed");
            Near(state.y, 0.0, "Connected foothold changed height");
            Check(state.grounded && state.footholdId == 2, "Foothold connection not applied");
            for (int i = 0; i < 10; ++i)
                simulation.Step(state, {});

            Near(state.x, 40.0, "Neutral input did not stop movement");
            for (int i = 0; i < 26; ++i)
                simulation.Step(state, { -1, false });

            Check(state.footholdId == 1 && state.grounded, "Previous foothold connection failed");
        });

        Run("Jump, gravity, air jump rejection and held key", [&]
        {
            MovementSimulation simulation(sample);
            PlatformMovementState state;
            simulation.Reset(state);
            simulation.Step(state, { 0, true });
            Check(!state.grounded && state.velocityY > 0.0, "Jump did not start");
            simulation.Step(state, { 0, false });
            double before = state.velocityY;
            simulation.Step(state, { 0, true });
            Check(state.velocityY < before, "Air jump accepted");
            double peak = state.y;
            for (int i = 0; i < 100; ++i)
            {
                Check(simulation.Step(state, { 0, true }) == SimulationResult::Success, "Jump state invalid");
                peak = (std::max)(peak, state.y);
            }

            Check(peak > 55.0 && peak < 61.0 && state.grounded, "Jump trajectory or landing incorrect");
            Near(state.y, 0.0, "Held jump repeated after landing");
            simulation.Step(state, { 0, false });
            simulation.Step(state, { 0, true });
            Check(!state.grounded && state.velocityY > 0.0, "Released key could not jump again");
        });

        Run("One way platform ascent and landing", [&]
        {
            MapGeometry geometry = sample;
            geometry.colliders.clear();
            geometry.footholds.resize(2);
            geometry.footholds.push_back({ 3, -100, 20, 100, 20, 0, 0 });
            MovementSimulation simulation(geometry);
            PlatformMovementState state;
            simulation.Reset(state);
            simulation.Step(state, { 0, true });
            bool passed = false;
            for (int i = 0; i < 100; ++i)
            {
                Check(simulation.Step(state, { 0, true }) == SimulationResult::Success, "Platform jump state invalid");
                passed = passed || state.y > 26.0;
            }

            Check(passed && state.grounded && state.footholdId == 3, "One way platform rule failed");
            Near(state.y, 26.0, "Foot did not align with platform");
        });

        Run("Fast fall lands on the first crossed platform", [&]
        {
            MapGeometry geometry = sample;
            geometry.movement.maxFallSpeed = 10000;
            MovementSimulation simulation(geometry);
            PlatformMovementState state{ 50.0, 150.0, 0.0, -10000.0 };
            Check(simulation.Step(state, {}) == SimulationResult::Success, "Fast fall failed");
            Check(state.grounded && state.footholdId == 4, "Skipped first platform");
            Near(state.y, 80.0, "Wrong landing height");
        });

        Run("Landing checks X at the crossing time", [&]
        {
            MapGeometry geometry = sample;
            geometry.movement.horizontalSpeed = 5000;
            geometry.movement.maxFallSpeed = 10000;
            MovementSimulation simulation(geometry);
            PlatformMovementState state{ 0.0, 100.0, 0.0, -10000.0 };
            simulation.Step(state, { 1, false });
            Near(state.x, 100.0, "Wrong horizontal displacement");
            Near(state.y, 0.0, "Used destination X for upper platform landing");
            Check(state.grounded && state.footholdId == 2, "Wrong crossing platform");
        });

        Run("Thin wall sweep and movement away from contact", [&]
        {
            MapGeometry geometry = sample;
            geometry.movement.horizontalSpeed = 10000;
            MovementSimulation simulation(geometry);
            PlatformMovementState state;
            simulation.Reset(state);
            simulation.Step(state, { 1, false });
            simulation.Step(state, { 1, false });
            Near(state.x, 214.0, "Tunneled through wall");
            Near(state.velocityX, 0.0, "Wall did not stop horizontal speed");
            simulation.Step(state, { -1, false });
            Near(state.x, 14.0, "Wall blocked movement away from contact");
        });

        Run("Collider roof landing uses explicit foothold", [&]
        {
            MapGeometry geometry = sample;
            geometry.movement.maxFallSpeed = 10000;
            MovementSimulation simulation(geometry);
            PlatformMovementState state{ 230.0, 150.0, 0.0, -10000.0 };
            simulation.Step(state, {});
            Check(state.grounded && state.footholdId == 5, "Roof support missing");
            Near(state.y, 86.0, "Roof height incorrect");
            simulation.Step(state, { 0, true });
            Check(!state.grounded && state.velocityY > 0.0, "Roof jump failed");
        });

        Run("Ceiling collision stops ascent", [&]
        {
            MapGeometry geometry = sample;
            geometry.colliders.push_back({ 2, -10, 20, 10, 30 });
            MovementSimulation simulation(geometry);
            PlatformMovementState state;
            simulation.Reset(state);
            for (int i = 0; i < 100; ++i)
            {
                Check(simulation.Step(state, { 0, true }) == SimulationResult::Success, "Ceiling state invalid");
                Check(state.y <= 14.0 + MovementSimulation::Epsilon, "Tunneled through ceiling");
            }

            Check(state.grounded, "Did not land after ceiling collision");
        });

        Run("Wall contact allows vertical sliding", [&]
        {
            MovementSimulation simulation(sample);
            PlatformMovementState state{ 210.0, 30.0, 0.0, -50.0 };
            for (int i = 0; i < 8; ++i)
                Check(simulation.Step(state, { 1, false }) == SimulationResult::Success, "Wall sliding state invalid");

            Near(state.x, 214.0, "Wall sliding changed contact X");
            Check(state.y < 20.0, "Wall prevented downward movement");
        });

        Run("Diagonal corner contact does not penetrate", [&]
        {
            MovementSimulation simulation(sample);
            PlatformMovementState state{ 212.4, 87.6, 0.0, -70.4 };
            Check(simulation.Step(state, { 1, false }) == SimulationResult::Success, "Corner step failed");
            Near(state.x, 214.0, "Corner X incorrect");
            Near(state.y, 86.0, "Corner Y incorrect");
            Check(simulation.Step(state, { -1, false }) == SimulationResult::Success, "Could not leave corner");
            Check(state.x < 214.0 && state.y < 86.0, "Corner blocked exit");
        });

        Run("Collision area stays inside side and top bounds", [&]
        {
            MovementSimulation simulation(sample);
            PlatformMovementState state{ 393.0, 100.0 };
            simulation.Step(state, { 1, false });
            Near(state.x, 394.0, "Body exceeded right boundary");
            state = { 0.0, 193.0, 0.0, 50.0 };
            simulation.Step(state, {});
            simulation.Step(state, {});
            Near(state.y, 194.0, "Body exceeded top boundary");
            Near(state.velocityY, 0.0, "Top boundary did not stop ascent");
        });

        Run("Walk off edge, fall and respawn", [&]
        {
            MovementSimulation simulation(sample);
            PlatformMovementState state{ 380.0, 0.0, 0.0, 0.0, 2, true };
            simulation.Step(state, { 1, false });
            Check(!state.grounded && state.footholdId == 0 && state.y < 0.0, "Edge did not start falling");
            bool respawned = false;
            state.jumpHeld = true;
            for (int i = 0; i < 150 && !respawned; ++i)
            {
                SimulationResult result = simulation.Step(state, { 0, true });
                Check(result == SimulationResult::Success || result == SimulationResult::Respawned, "Fall state invalid");
                respawned = result == SimulationResult::Respawned;
            }

            Check(respawned && state.grounded && state.footholdId == 1, "Respawn not applied");
            Near(state.x, 0.0, "Respawn X incorrect");
            Near(state.y, 0.0, "Respawn Y incorrect");
            simulation.Step(state, { 0, true });
            Check(state.grounded, "Held jump repeated after respawn");
        });

        Run("Invalid input and state leave state unchanged", [&]
        {
            MovementSimulation simulation(sample);
            PlatformMovementState state;
            simulation.Reset(state);
            Check(simulation.Step(state, { 2, true }) == SimulationResult::InvalidInput, "Invalid direction accepted");
            Check(state.grounded && !state.jumpHeld && state.x == 0.0 && state.y == 0.0, "Invalid input changed state");
            state.velocityY = std::numeric_limits<double>::quiet_NaN();
            Check(simulation.Step(state, {}) == SimulationResult::InvalidState && std::isnan(state.velocityY), "Non finite state accepted or changed");
            state = { 230.0, 20.0 };
            Check(simulation.Step(state, {}) == SimulationResult::InvalidState, "State inside wall accepted");
        });

        Run("Free mode is not simulated as Platformer", [&]
        {
            MovementSimulation simulation(geometries.at(100000001));
            PlatformMovementState state{ 100.0, 50.0 };
            Check(!simulation.Reset(state) && simulation.Step(state, {}) == SimulationResult::NotPlatformer, "Free mode simulated");
            Near(state.x, 100.0, "Free mode state changed");
        });

        Run("Identical fixed tick inputs produce identical results", [&]
        {
            MovementSimulation simulation(sample);
            PlatformMovementState first;
            PlatformMovementState second;
            simulation.Reset(first);
            simulation.Reset(second);
            for (int i = 0; i < 200; ++i)
            {
                PlatformMovementInput input{ static_cast<int8_t>((i / 25) % 2 == 0 ? 1 : -1), i % 50 < 10 };
                Check(simulation.Step(first, input) == simulation.Step(second, input), "Result was not deterministic");
                Check(first.x == second.x && first.y == second.y && first.velocityY == second.velocityY && first.footholdId == second.footholdId, "State was not deterministic");
            }
        });

        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[FAIL] " << exception.what() << '\n';
        return 1;
    }
}
