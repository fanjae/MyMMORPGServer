#pragma once

#include <cstdint>
#include <vector>

// TCP stream에서 수신한 데이터를 누적하는 선형 버퍼.
// [_readPos, _writePos) 구간이 아직 처리되지 않은 유효 데이터이며,
// Clean()으로 소비된 앞쪽 영역을 제거해 남은 데이터를 앞으로 당긴다.
class RecvBuffer
{
public:
    explicit RecvBuffer(int32_t bufferSize);

    char* WritePos() { return _buffer.data() + _writePos; }
    char* ReadPos() { return _buffer.data() + _readPos; }

    int32_t WritableSize() const { return static_cast<int32_t>(_buffer.size()) - _writePos; }
    int32_t DataSize() const { return _writePos - _readPos; }

    bool OnWrite(int32_t bytes);
    bool OnRead(int32_t bytes);
    void Clean();

private:
    std::vector<char> _buffer;
    int32_t _readPos = 0;
    int32_t _writePos = 0;
};