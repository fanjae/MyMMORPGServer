#include "../../GameServer/Player.h"

#include <functional>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
}

void RunMovementAcknowledgementTests()
{
    auto run = [](const char* name, const std::function<void()>& test) { test(); std::cout << "[PASS] " << name << '\n'; };
    run("Movement acknowledgement counts simulated steps rather than packet receipt", []
    {
        Player player(1, 1, "player", 1);
        player.BeginMap();
        MovementInputPacket input{100000000, player.GetGeneration(), 1, 1, 0};
        Check(player.AcceptInput(input) && player.GetInputSequence() == 0 && player.GetInputTicks() == 0, "Receipt acknowledged unsimulated input");
        bool expired;
        auto now = std::chrono::steady_clock::now();
        for (int i = 1; i <= 4; ++i)
        {
            player.ConsumeInput(now, expired);
            Check(!expired && player.GetInputSequence() == 1 && player.GetInputTicks() == static_cast<uint32_t>(i), "Applied steps not counted");
        }
        input.sequence = 2;
        Check(player.AcceptInput(input) && player.GetInputTicks() == 4, "Heartbeat reset age before simulation");
        player.ConsumeInput(now, expired);
        Check(player.GetInputSequence() == 2 && player.GetInputTicks() == 1, "Heartbeat acknowledgement did not reset per-input age");
    });
    run("Short jump press and release preserve one simulation pulse and final latch", []
    {
        Player player(1, 1, "player", 1);
        player.BeginMap();
        MovementInputPacket press{100000000, player.GetGeneration(), 1, 0, 1};
        MovementInputPacket release{100000000, player.GetGeneration(), 2, 0, 0};
        Check(player.AcceptInput(press) && player.AcceptInput(release), "Jump inputs rejected");
        bool expired;
        auto now = std::chrono::steady_clock::now();
        Check(player.ConsumeInput(now, expired).jumpHeld, "Short jump lost between ticks");
        player.FinishInput();
        Check(!player.GetPlatformState().jumpHeld && player.GetInputSequence() == 2 && player.GetInputTicks() == 1, "Final jump latch not authoritative");
        Check(!player.ConsumeInput(now, expired).jumpHeld && player.GetInputTicks() == 2, "Short jump repeated");
    });
    run("Expired movement neutralizes intent while acknowledgement remains monotonic", []
    {
        Player player(1, 1, "player", 1);
        player.BeginMap();
        Check(player.AcceptInput(MovementInputPacket{100000000, player.GetGeneration(), 1, 1, 1}), "Input rejected");
        bool expired;
        auto now = std::chrono::steady_clock::now();
        player.ConsumeInput(now, expired);
        auto input = player.ConsumeInput(now + std::chrono::milliseconds(300), expired);
        player.FinishInput();
        Check(expired && input.horizontal == 0 && !input.jumpHeld && !player.GetPlatformState().jumpHeld && player.GetInputSequence() == 1 && player.GetInputTicks() == 2, "Expired input or age invalid");
    });
    run("New map clears acknowledgement age and rejects previous generation", []
    {
        Player player(1, 1, "player", 1);
        player.BeginMap();
        MovementInputPacket old{100000000, player.GetGeneration(), 1, 1, 0};
        Check(player.AcceptInput(old), "Input rejected");
        bool expired;
        player.ConsumeInput(std::chrono::steady_clock::now(), expired);
        player.BeginMap();
        Check(player.GetInputSequence() == 0 && player.GetInputTicks() == 0 && !player.AcceptInput(old), "Map retained previous acknowledgement");
    });
}
