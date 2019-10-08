#include "rwte/utf8.h"

#include <array>
#include <cstring>
#include <tuple>

// todo: enum classes

enum utf8_result
{
    UTF8_RES_CONTINUE,
    UTF8_RES_COMPLETE,
    UTF8_RES_INVALID
};

enum utf8_state_enum
{
    UTF8_GROUND,
    UTF8_U3_B2_E0,
    UTF8_U3_B2_ED,
    UTF8_U4_B3_F0,
    UTF8_U4_B3_F4,
    UTF8_TAIL1,
    UTF8_TAIL2,
    UTF8_TAIL3
};

enum utf8_action_enum
{
    ACT_NOOP,
    ACT_EMIT,
    ACT_SET_BYTE1_EMIT,
    ACT_SET_BYTE2,
    ACT_SET_BYTE3,
    ACT_SET_BYTE2_TOP,
    ACT_SET_BYTE3_TOP,
    ACT_SET_BYTE4_TOP,
    ACT_INVALID
};

constexpr unsigned char transition(unsigned char action, unsigned char state)
{
    return action << 4 | state;
}

constexpr auto make_transitions()
{
    std::array<std::array<unsigned char, 256>, 8> arr{};
    unsigned char invalid_val = transition(ACT_INVALID, UTF8_GROUND);

    for (int i = 0; i < 256; i++) {
        if (0x00 <= i && i <= 0x7F)
            arr[UTF8_GROUND][i] = transition(ACT_EMIT, UTF8_GROUND);
        else if (0xC2 <= i && i <= 0xDF)
            arr[UTF8_GROUND][i] = transition(ACT_SET_BYTE2_TOP, UTF8_TAIL1);
        else if (0xE0 <= i && i <= 0xE0)
            arr[UTF8_GROUND][i] = transition(ACT_NOOP, UTF8_U3_B2_E0);
        else if (0xE1 <= i && i <= 0xEC)
            arr[UTF8_GROUND][i] = transition(ACT_SET_BYTE3_TOP, UTF8_TAIL2);
        else if (0xED <= i && i <= 0xED)
            arr[UTF8_GROUND][i] = transition(ACT_SET_BYTE3_TOP, UTF8_U3_B2_ED);
        else if (0xEE <= i && i <= 0xEF)
            arr[UTF8_GROUND][i] = transition(ACT_SET_BYTE3_TOP, UTF8_TAIL2);
        else if (0xF0 <= i && i <= 0xF0)
            arr[UTF8_GROUND][i] = transition(ACT_SET_BYTE4_TOP, UTF8_U4_B3_F0);
        else if (0xF1 <= i && i <= 0xF3)
            arr[UTF8_GROUND][i] = transition(ACT_SET_BYTE4_TOP, UTF8_TAIL3);
        else if (0xF4 <= i && i <= 0xF4)
            arr[UTF8_GROUND][i] = transition(ACT_SET_BYTE4_TOP, UTF8_U4_B3_F4);
        else
            arr[UTF8_GROUND][i] = invalid_val;

        if (0xA0 <= i && i <= 0xBF)
            arr[UTF8_U3_B2_E0][i] = transition(ACT_SET_BYTE2, UTF8_TAIL1);
        else
            arr[UTF8_U3_B2_E0][i] = invalid_val;

        if (0x80 <= i && i <= 0x9F)
            arr[UTF8_U3_B2_ED][i] = transition(ACT_SET_BYTE2, UTF8_TAIL1);
        else
            arr[UTF8_U3_B2_ED][i] = invalid_val;

        if (0x90 <= i && i <= 0xBF)
            arr[UTF8_U4_B3_F0][i] = transition(ACT_SET_BYTE3, UTF8_TAIL2);
        else
            arr[UTF8_U4_B3_F0][i] = invalid_val;

        if (0x80 <= i && i <= 0x8F)
            arr[UTF8_U4_B3_F4][i] = transition(ACT_SET_BYTE3, UTF8_TAIL2);
        else
            arr[UTF8_U4_B3_F4][i] = invalid_val;

        if (0x80 <= i && i <= 0xBF)
            arr[UTF8_TAIL1][i] = transition(ACT_SET_BYTE1_EMIT, UTF8_GROUND);
        else
            arr[UTF8_TAIL1][i] = invalid_val;

        if (0x80 <= i && i <= 0xBF)
            arr[UTF8_TAIL2][i] = transition(ACT_SET_BYTE2, UTF8_TAIL1);
        else
            arr[UTF8_TAIL2][i] = invalid_val;

        if (0x80 <= i && i <= 0xBF)
            arr[UTF8_TAIL3][i] = transition(ACT_SET_BYTE3, UTF8_TAIL2);
        else
            arr[UTF8_TAIL3][i] = invalid_val;
    }

    return arr;
}

constexpr auto UTF8_TRANSITIONS = make_transitions();

class Utf8Decoder
{
public:
    constexpr std::pair<utf8_result, char32_t> feed(unsigned char b);

private:
    char32_t m_codepoint = 0;
    utf8_state_enum m_state = UTF8_GROUND;
};

constexpr std::pair<utf8_result, char32_t> Utf8Decoder::feed(unsigned char b)
{
    auto chg = UTF8_TRANSITIONS[m_state][b];
    auto action = static_cast<utf8_action_enum>((chg & 0xF0) >> 4);
    auto newstate = static_cast<utf8_state_enum>(chg & 0x0F);

    auto res = UTF8_RES_CONTINUE;
    char32_t cp = 0;

    switch (action) {
        case ACT_NOOP:
            break;
        case ACT_EMIT:
            res = UTF8_RES_COMPLETE;
            cp = b;
            m_codepoint = 0;
            break;
        case ACT_SET_BYTE1_EMIT:
            m_codepoint |= static_cast<char32_t>(b & 0b00111111);
            res = UTF8_RES_COMPLETE;
            cp = m_codepoint;
            m_codepoint = 0;
            break;
        case ACT_SET_BYTE2:
            m_codepoint |= (static_cast<char32_t>(b & 0b00111111) << 6);
            break;
        case ACT_SET_BYTE3:
            m_codepoint |= (static_cast<char32_t>(b & 0b00111111) << 12);
            break;
        case ACT_SET_BYTE2_TOP:
            m_codepoint |= (static_cast<char32_t>(b & 0b00011111) << 6);
            break;
        case ACT_SET_BYTE3_TOP:
            m_codepoint |= (static_cast<char32_t>(b & 0b00001111) << 12);
            break;
        case ACT_SET_BYTE4_TOP:
            m_codepoint |= (static_cast<char32_t>(b & 0b00000111) << 18);
            break;
        case ACT_INVALID:
            res = UTF8_RES_INVALID;
            cp = utf_invalid;
            m_codepoint = 0;
            break;
    }

    m_state = newstate;
    return {res, cp};
}

std::size_t utf8encode(char32_t cp, char* c)
{
    if (cp <= 0x0000007F) {
        // 0xxxxxxx
        c[0] = cp & 0b01111111;

        return 1;
    } else if (cp <= 0x000007FF) {
        // 110xxxxx
        c[0] = 0b11000000 | (cp >> 6 & 0b00011111);
        // 10xxxxxx
        c[1] = 0b10000000 | (cp & 0b00111111);

        return 2;
    } else if (cp <= 0x0000FFFF) {
        if (0x0000D800 <= cp && cp <= 0x0000DFFF) {
            // excluded range
            return 0;
        } else {
            // 1110xxxx
            c[0] = 0b11100000 | (cp >> 12 & 0b00001111);
            // 10xxxxxx
            c[1] = 0b10000000 | (cp >> 6 & 0b00111111);
            // 10xxxxxx
            c[2] = 0b10000000 | (cp & 0b00111111);

            return 3;
        }
    } else if (cp <= 0x0010FFFF) {
        // 11110xxx
        c[0] = 0b11110000 | (cp >> 18 & 0b00000111);
        // 10xxxxxx
        c[1] = 0b10000000 | (cp >> 12 & 0b00111111);
        // 10xxxxxx
        c[2] = 0b10000000 | (cp >> 6 & 0b00111111);
        // 10xxxxxx
        c[3] = 0b10000000 | (cp & 0b00111111);

        return 4;
    }

    return 0;
}

std::pair<std::size_t, char32_t> utf8decode(std::string_view c)
{
    if (!c.empty()) {
        Utf8Decoder dec;
        utf8_result state;
        std::size_t i = 0;
        char32_t cp = utf_invalid;
        while (i < c.size()) {
            std::tie(state, cp) = dec.feed(c[i]);
            if (state == UTF8_RES_CONTINUE)
                i++;
            else
                return {i + 1, cp};
        }
    }

    return {0, utf_invalid};
}

bool utf8contains(std::string_view s, char32_t cp)
{
    while (!s.empty()) {
        auto [len, curr] = utf8decode(s);
        if (!len)
            break;
        if (curr == cp)
            return true;
        s = s.substr(len);
    }

    return false;
}
