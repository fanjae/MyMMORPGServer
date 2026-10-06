#include "../../GameServer/ChatService.h"
#include "../../GameServer/PlayerManager.h"
#include "../../GameServer/Player.h"
#include "../../GameServer/Map.h"
#include "../../ServerCore/Session.h"
#include "../../ServerCore/SocketUtils.h"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    class ChatSession : public Session
    {
    public:
        ChatSession() : Session(SocketUtils::CreateSocket()) {}
        bool Send(const char* data, int32_t size) override
        {
            if (fail || !IsConnected())
                return false;
            packets.emplace_back(data, data + size);
            return true;
        }
        template<typename T> T Read(GamePacketOpcode opcode) const
        {
            Check(!packets.empty(), "Chat response missing");
            const auto& bytes = packets.back();
            PacketHeader header;
            memcpy(&header, bytes.data(), sizeof(header));
            Check(header.opcode == static_cast<uint16_t>(opcode) && bytes.size() == sizeof(header) + sizeof(T), "Chat response opcode or size");
            T payload;
            memcpy(&payload, bytes.data() + sizeof(header), sizeof(payload));
            return payload;
        }
        bool fail = false;
        std::vector<std::vector<char>> packets;
    protected:
        bool OnPacket(uint16_t, const char*, uint16_t) override { return true; }
    };

    struct Fixture
    {
        PlayerManager players;
        Map first{MapDefinition{100000000, 0, 0, -400, 400, -200, 200, 80, 12}};
        Map second{MapDefinition{100000001, 0, 0, -400, 400, -200, 200, 80, 12}};
        Player a{1001, 1, u8"발신자", 1}, b{2001, 2, u8"수신자", 1}, c{3001, 3, "observer", 1};
        ChatSession sa, sb, sc;
        ChatService service{players};
        ChatLimiter::Clock::time_point now{};
        Fixture()
        {
            a.SetSession(&sa); b.SetSession(&sb); c.SetSession(&sc);
            Check(players.Add(a) && players.Add(b) && players.Add(c), "Chat fixture player registration");
            Check(first.AddPlayer(a) && second.AddPlayer(b) && first.AddPlayer(c), "Chat fixture map registration");
        }
        void Clear() { sa.packets.clear(); sb.packets.clear(); sc.packets.clear(); }
    };

    ChatRequest MapMessage(const std::string& text)
    {
        ChatRequest request;
        Check(text.size() < sizeof(request.message), "Test message too long");
        memcpy(request.message, text.data(), text.size());
        return request;
    }

    WhisperRequest Whisper(uint32_t target, const std::string& text)
    {
        WhisperRequest request;
        request.targetCharacterId = target;
        memcpy(request.message, text.data(), text.size());
        return request;
    }
}

void RunChatTests()
{
    auto run = [](const char* name, const std::function<void()>& test) { test(); std::cout << "[PASS] " << name << '\n'; };
    run("Validated map chat trims Unicode whitespace and remains map-local", []
    {
        Fixture f;
        Check(f.service.SendMap(f.a, MapMessage(u8"　 안녕하세요 　"), f.now), "Map chat failed");
        auto packet = f.sa.Read<PlayerChat>(GamePacketOpcode::PlayerChat);
        Check(packet.characterId == 1001 && std::string(packet.message) == u8"안녕하세요", "Trim or authenticated sender");
        Check(f.sa.packets.size() == 1 && f.sc.packets.size() == 1 && f.sb.packets.empty(), "Map chat leaked or duplicated");
    });
    run("Cross-map whisper reaches only sender and target with authenticated names", []
    {
        Fixture f;
        Check(f.service.SendWhisper(f.a, Whisper(2001, u8"다른 맵 귓속말"), f.now), "Whisper failed");
        auto packet = f.sb.Read<WhisperMessage>(GamePacketOpcode::WhisperMessage);
        Check(packet.senderCharacterId == 1001 && packet.targetCharacterId == 2001 && std::string(packet.senderName) == u8"발신자" && std::string(packet.targetName) == u8"수신자" && std::string(packet.message) == u8"다른 맵 귓속말", "Whisper identity or UTF-8 changed");
        Check(f.sa.packets.size() == 1 && f.sb.packets.size() == 1 && f.sc.packets.empty(), "Whisper leaked or duplicated");
    });
    run("Self whisper is delivered once", []
    {
        Fixture f;
        Check(f.service.SendWhisper(f.a, Whisper(1001, "self"), f.now), "Self whisper failed");
        Check(f.sa.packets.size() == 1 && f.sb.packets.empty() && f.sc.packets.empty(), "Self whisper duplicated or leaked");
        Check(f.sa.Read<WhisperMessage>(GamePacketOpcode::WhisperMessage).targetCharacterId == 1001, "Self target changed");
    });
    run("Invalid and offline whisper targets return recoverable errors", []
    {
        Fixture f;
        for (uint32_t target : {0u, 9999u, 2001u})
        {
            if (target == 2001) f.sb.Close();
            Check(f.service.SendWhisper(f.a, Whisper(target, "hello"), f.now), "Target error disconnected sender");
            auto result = f.sa.Read<ChatResponse>(GamePacketOpcode::ChatResponse);
            Check(result.operation == ChatOperation::Whisper && result.targetCharacterId == target && result.result == (target == 0 ? ChatResult::InvalidTarget : ChatResult::TargetNotFound), "Wrong target result");
        }
        Check(f.sa.IsConnected() && f.sc.packets.empty() && f.sb.packets.empty(), "Target error affected other sessions");
    });
    run("Whisper recipient send failure preserves sender and schedules target cleanup", []
    {
        Fixture f;
        f.sb.fail = true;
        Check(f.service.SendWhisper(f.a, Whisper(2001, "hello"), f.now), "Recipient failure disconnected sender");
        Check(f.sa.Read<ChatResponse>(GamePacketOpcode::ChatResponse).result == ChatResult::DeliveryFailed && f.sb.IsCloseRequested() && f.sa.IsConnected(), "Recipient failure not isolated");
        Check(f.service.SendMap(f.a, MapMessage("still alive"), f.now) && f.sc.packets.size() == 1, "Sender cannot chat after recipient failure");
    });
    run("Malformed UTF-8, embedded NUL, controls, empty and unterminated chat never broadcast", []
    {
        Fixture f;
        std::vector<ChatRequest> invalid;
        for (const std::string& text : {std::string(""), std::string(u8"　 \t"), std::string("\xc0\x80"), std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"), std::string("a\nline"), std::string("a\xc2\x85line"), std::string("a\x7f"), std::string("a\0b", 3), std::string("\xe3\x81")})
            invalid.push_back(MapMessage(text));
        ChatRequest unterminated;
        memset(unterminated.message, 'x', sizeof(unterminated.message));
        invalid.push_back(unterminated);
        for (const auto& request : invalid)
        {
            Check(f.service.SendMap(f.a, request, f.now), "Invalid chat disconnected sender");
            Check(f.sa.Read<ChatResponse>(GamePacketOpcode::ChatResponse).result == ChatResult::InvalidMessage, "Malformed chat accepted");
            f.now += std::chrono::seconds(2);
        }
        Check(f.sc.packets.empty() && f.sb.packets.empty() && f.sa.IsConnected(), "Malformed chat broadcast or closed session");
        auto badWhisper = Whisper(2001, "a\nb");
        Check(f.service.SendWhisper(f.a, badWhisper, f.now) && f.sa.Read<ChatResponse>(GamePacketOpcode::ChatResponse).result == ChatResult::InvalidMessage && f.sb.packets.empty(), "Whisper bypassed validation");
    });
    run("127-byte UTF-8 chat boundary is preserved without truncation", []
    {
        Fixture f;
        std::string text;
        for (int i = 0; i < 42; ++i) text += u8"가";
        text += 'a';
        Check(f.service.SendWhisper(f.a, Whisper(2001, text), f.now), "Boundary chat failed");
        Check(std::string(f.sb.Read<WhisperMessage>(GamePacketOpcode::WhisperMessage).message) == text, "UTF-8 boundary truncated");
    });
    run("Shared account chat budget survives map changes, reconnect and cleanup then recovers", []
    {
        Fixture f;
        for (int i = 0; i < 5; ++i)
            Check(i % 2 == 0 ? f.service.SendMap(f.a, MapMessage("burst"), f.now) : f.service.SendWhisper(f.a, Whisper(2001, "burst"), f.now), "Burst allowance rejected");
        f.Clear();
        Check(f.service.SendWhisper(f.a, Whisper(2001, "limited"), f.now), "Rate limit disconnected sender");
        Check(f.sa.Read<ChatResponse>(GamePacketOpcode::ChatResponse).result == ChatResult::RateLimited && f.sb.packets.empty(), "Map and whisper do not share budget");
        f.first.RemovePlayer(f.a);
        Check(f.second.AddPlayer(f.a), "Map transition failed");
        f.players.Remove(f.a);
        Player replacement(1002, 1, "replacement", 1);
        replacement.SetSession(&f.sa);
        Check(f.players.Add(replacement) && f.first.AddPlayer(replacement), "Reconnect registration failed");
        f.now += std::chrono::milliseconds(250);
        f.players.GetChatLimiter().Cleanup(f.now);
        Check(f.service.SendMap(replacement, MapMessage("retry"), f.now), "Reconnect limit disconnected sender");
        auto result = f.sa.Read<ChatResponse>(GamePacketOpcode::ChatResponse);
        Check(result.result == ChatResult::RateLimited && result.retryAfterMs == 750 && f.sc.packets.empty(), "Reconnect or cleanup reset account budget");
        Check(f.service.SendMap(f.c, MapMessage("other account"), f.now), "Other account incorrectly limited");
        f.now += std::chrono::milliseconds(750);
        Check(f.service.SendMap(replacement, MapMessage("recovered"), f.now) && f.sa.Read<PlayerChat>(GamePacketOpcode::PlayerChat).characterId == 1002, "Recovery delayed by rejected requests");
        f.now += std::chrono::seconds(5);
        f.players.GetChatLimiter().Cleanup(f.now);
        for (int i = 0; i < 5; ++i)
            Check(f.service.SendMap(replacement, MapMessage("refilled"), f.now) && f.sa.Read<PlayerChat>(GamePacketOpcode::PlayerChat).characterId == 1002, "Idle cleanup did not restore full capacity");
    });
}
