#pragma once

#include <cstdint>
#include <string>

inline bool ReadUtf8Scalar(const std::string& text, size_t& index, uint32_t& codepoint)
{
    if (index >= text.size())
        return false;
    auto first = static_cast<unsigned char>(text[index++]);
    codepoint = first;
    uint32_t remaining = 0;
    uint32_t minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) { codepoint = first & 0x1f; remaining = 1; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { codepoint = first & 0x0f; remaining = 2; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { codepoint = first & 0x07; remaining = 3; minimum = 0x10000; }
    else if (first >= 0x80)
        return false;
    if (text.size() - index < remaining)
        return false;
    for (uint32_t count = 0; count < remaining; ++count)
    {
        auto next = static_cast<unsigned char>(text[index++]);
        if ((next & 0xc0) != 0x80)
            return false;
        codepoint = (codepoint << 6) | (next & 0x3f);
    }
    // 잘린 UTF-8, 중복 표현과 surrogate는 정상 문자열로 전달하지 않는다.
    return codepoint >= minimum && codepoint <= 0x10ffff && !(codepoint >= 0xd800 && codepoint <= 0xdfff);
}

inline bool IsUnicodeWhitespace(uint32_t value)
{
    return (value >= 9 && value <= 13) || value == 0x20 || value == 0x85 || value == 0xa0 || value == 0x1680 ||
        (value >= 0x2000 && value <= 0x200a) || value == 0x2028 || value == 0x2029 || value == 0x202f || value == 0x205f || value == 0x3000;
}
