#ifndef RWTE_UTF8_H
#define RWTE_UTF8_H

#include <array>
#include <cstdint>
#include <string>
#include <utility>

constexpr int utf_size = 4;
constexpr char32_t utf_invalid = 0xFFFD;

std::size_t utf8size(std::string_view c);
std::pair<std::size_t, char32_t> utf8decode(std::string_view c);
bool utf8contains(std::string_view s, char32_t cp);

template <typename OutputIt, typename OutType = char>
constexpr OutputIt utf8encode(char32_t cp, OutputIt dest)
{
    if (cp <= 0x0000007F) {
        // 0xxxxxxx
        *dest++ = static_cast<OutType>(cp & 0b01111111);
    } else if (cp <= 0x000007FF) {
        // 110xxxxx
        *dest++ = static_cast<OutType>(0b11000000 | (cp >> 6 & 0b00011111));
        // 10xxxxxx
        *dest++ = static_cast<OutType>(0b10000000 | (cp & 0b00111111));
    } else if (cp <= 0x0000FFFF) {
        if (0x0000D800 <= cp && cp <= 0x0000DFFF) {
            // excluded range
        } else {
            // 1110xxxx
            *dest++ = static_cast<OutType>(0b11100000 | (cp >> 12 & 0b00001111));
            // 10xxxxxx
            *dest++ = static_cast<OutType>(0b10000000 | (cp >> 6 & 0b00111111));
            // 10xxxxxx
            *dest++ = static_cast<OutType>(0b10000000 | (cp & 0b00111111));
        }
    } else if (cp <= 0x0010FFFF) {
        // 11110xxx
        *dest++ = static_cast<OutType>(0b11110000 | (cp >> 18 & 0b00000111));
        // 10xxxxxx
        *dest++ = static_cast<OutType>(0b10000000 | (cp >> 12 & 0b00111111));
        // 10xxxxxx
        *dest++ = static_cast<OutType>(0b10000000 | (cp >> 6 & 0b00111111));
        // 10xxxxxx
        *dest++ = static_cast<OutType>(0b10000000 | (cp & 0b00111111));
    }

    return dest;
}

#endif // RWTE_UTF8_H
