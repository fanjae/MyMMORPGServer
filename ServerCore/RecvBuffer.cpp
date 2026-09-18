#include "pch.h"
#include "RecvBuffer.h"

RecvBuffer::RecvBuffer(int32_t bufferSize) : _buffer(bufferSize)
{
}

bool RecvBuffer::OnWrite(int32_t bytes)
{
    if (bytes < 0 || bytes > WritableSize())
        return false;

    _writePos += bytes;
    return true;
}

bool RecvBuffer::OnRead(int32_t bytes)
{
    if (bytes < 0 || bytes > DataSize())
        return false;

    _readPos += bytes;
    return true;
}

void RecvBuffer::Clean()
{
    if (_readPos == _writePos)
    {
        _readPos = 0;
        _writePos = 0;
        return;
    }

    if (_readPos == 0)
        return;

    int32_t dataSize = DataSize();

    // 아직 처리하지 않은 데이터를 버퍼 앞쪽으로 이동
    memmove(_buffer.data(), ReadPos(), dataSize);

    _readPos = 0;
    _writePos = dataSize;
}