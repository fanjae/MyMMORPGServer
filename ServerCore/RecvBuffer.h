#pragma once

#include <cstdint>
#include <vector>

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