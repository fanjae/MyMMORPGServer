#include "ChatService.h"
#include "Map.h"
#include "Player.h"
#include "PlayerManager.h"
#include "../Protocol/Utf8String.h"
#include "../ServerCore/Session.h"

#include <cstring>
#include <string>

namespace
{
    template<typename T>
    bool SendPacket(Player& player, GamePacketOpcode opcode, const T& payload)
    {
        Session* session = player.GetSession();
        if (session == nullptr || !session->IsConnected())
            return false;
        PacketHeader header{static_cast<uint16_t>(sizeof(PacketHeader) + sizeof(T)), static_cast<uint16_t>(opcode)};
        char buffer[sizeof(header) + sizeof(T)];
        memcpy(buffer, &header, sizeof(header));
        memcpy(buffer + sizeof(header), &payload, sizeof(payload));
        return session->Send(buffer, sizeof(buffer));
    }

    bool SendResult(Player& sender, ChatOperation operation, ChatResult result, uint32_t target = 0, uint32_t retry = 0)
    {
        return SendPacket(sender, GamePacketOpcode::ChatResponse, ChatResponse{operation, result, target, retry});
    }

    bool ReadMessage(const char* buffer, std::string& message)
    {
        const char* end = static_cast<const char*>(memchr(buffer, '\0', MAX_CHAT_MESSAGE_LENGTH));
        if (end == nullptr)
            return false;
        for (const char* padding = end + 1; padding < buffer + MAX_CHAT_MESSAGE_LENGTH; ++padding)
            if (*padding != '\0')
                return false;
        std::string text(buffer, end);
        size_t begin = text.size();
        size_t finish = 0;
        for (size_t index = 0; index < text.size();)
        {
            size_t start = index;
            uint32_t scalar;
            if (!ReadUtf8Scalar(text, index, scalar))
                return false;
            if (IsUnicodeWhitespace(scalar))
                continue;
            if (scalar < 0x20 || (scalar >= 0x7f && scalar <= 0x9f))
                return false;
            if (begin == text.size())
                begin = start;
            finish = index;
        }
        if (finish <= begin)
            return false;
        message = text.substr(begin, finish - begin);
        // 본문 안의 개행과 탭은 거절해 채팅을 한 줄 입력으로 유지한다.
        for (size_t index = 0; index < message.size();)
        {
            uint32_t scalar;
            if (!ReadUtf8Scalar(message, index, scalar) || (scalar >= 9 && scalar <= 13) || scalar == 0x85 || scalar == 0x2028 || scalar == 0x2029)
                return false;
        }
        return true;
    }
}

bool ChatService::SendMap(Player& sender, const ChatRequest& request, ChatLimiter::Clock::time_point now)
{
    uint32_t retry;
    if (!_players.GetChatLimiter().TryConsume(sender.GetAccountId(), retry, now))
        return SendResult(sender, ChatOperation::Map, ChatResult::RateLimited, 0, retry);
    std::string message;
    if (!ReadMessage(request.message, message))
        return SendResult(sender, ChatOperation::Map, ChatResult::InvalidMessage);
    if (sender.GetMap() == nullptr)
        return false;
    // 정상 Map 채팅은 기존 echo를 유지하고 거절된 요청만 별도 결과를 전달한다.
    return sender.GetMap()->NotifyPlayerChat(sender, message.c_str());
}

bool ChatService::SendWhisper(Player& sender, const WhisperRequest& request, ChatLimiter::Clock::time_point now)
{
    uint32_t retry;
    if (!_players.GetChatLimiter().TryConsume(sender.GetAccountId(), retry, now))
        return SendResult(sender, ChatOperation::Whisper, ChatResult::RateLimited, request.targetCharacterId, retry);
    std::string message;
    if (!ReadMessage(request.message, message))
        return SendResult(sender, ChatOperation::Whisper, ChatResult::InvalidMessage, request.targetCharacterId);
    if (request.targetCharacterId == 0)
        return SendResult(sender, ChatOperation::Whisper, ChatResult::InvalidTarget);
    Player* target = _players.FindByCharacterId(request.targetCharacterId);
    if (target == nullptr || target->GetSession() == nullptr || !target->GetSession()->IsConnected())
        return SendResult(sender, ChatOperation::Whisper, ChatResult::TargetNotFound, request.targetCharacterId);

    WhisperMessage packet;
    packet.senderCharacterId = sender.GetCharacterId();
    packet.targetCharacterId = target->GetCharacterId();
    if (!CopyCharacterName(packet.senderName, sender.GetName()) || !CopyCharacterName(packet.targetName, target->GetName()))
        return false;
    memcpy(packet.message, message.data(), message.size());
    // Map 브로드캐스트를 사용하지 않고 접속 중인 수신자와 발신자에게만 전달한다.
    if (!SendPacket(*target, GamePacketOpcode::WhisperMessage, packet))
    {
        target->GetSession()->RequestClose();
        return SendResult(sender, ChatOperation::Whisper, ChatResult::DeliveryFailed, request.targetCharacterId);
    }
    return target == &sender || SendPacket(sender, GamePacketOpcode::WhisperMessage, packet);
}
