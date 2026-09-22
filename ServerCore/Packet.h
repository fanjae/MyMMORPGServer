#pragma once

#include <cstdint>

constexpr uint16_t MAX_PACKET_SIZE = 4096;

// 패킷 구조체를 그대로 wire format으로 복사하므로
// 컴파일러 padding이 포함되지 않도록 1바이트 정렬을 사용한다.
#pragma pack(push, 1)

struct PacketHeader
{
    uint16_t size = 0;   // PacketHeader를 포함한 전체 패킷 크기
    uint16_t opcode = 0; // payload의 종류를 식별하는 패킷 ID
};

#pragma pack(pop)