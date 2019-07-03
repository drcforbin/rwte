#include "rwte/catch.hpp"
#include "rwte/utf8.h"

#include <array>
#include <cstring>

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

enum utf_action_enum
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

constexpr unsigned char transition(unsigned char action, unsigned char state) {
    return action << 4 | state;
}

constexpr auto make_transitions() {
    std::array<std::array<unsigned char, 256>, 8> arr {};
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
    Utf8Decoder() : m_codepoint(0), m_state(UTF8_GROUND) {}

    utf8_result feed(unsigned char b, char32_t *cp);

private:
    char32_t m_codepoint;
    utf8_state_enum m_state;
};


utf8_result Utf8Decoder::feed(unsigned char b, char32_t *cp)
{
    unsigned char chg = UTF8_TRANSITIONS[m_state][b];
    int action = (chg & 0xF0) >> 4;
    utf8_state_enum newstate = static_cast<utf8_state_enum>(chg & 0x0F);

    utf8_result res = UTF8_RES_CONTINUE;

    switch (action)
    {
    case ACT_NOOP:
        break;
    case ACT_EMIT:
        *cp = b;
        res = UTF8_RES_COMPLETE;
        m_codepoint = 0;
        break;
    case ACT_SET_BYTE1_EMIT:
        m_codepoint |= static_cast<char32_t>(b & 0b00111111);
        *cp = m_codepoint;
        res = UTF8_RES_COMPLETE;
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
        *cp = utf_invalid;
        res = UTF8_RES_INVALID;
        m_codepoint = 0;
        break;
    }

    m_state = newstate;
    return res;
}

std::size_t utf8decode(const char *c, char32_t *u, std::size_t clen)
{
    *u = utf_invalid;
    if (!clen)
        return 0;

    Utf8Decoder dec;

    std::size_t i = 0;
    while (i < clen)
    {
        if (dec.feed(c[i], u) == UTF8_RES_CONTINUE)
            i++;
        else
            return i+1;
    }

    return 0;
}

std::size_t utf8encode(char32_t cp, char *c)
{
    if (cp <= 0x0000007F)
    {
        // 0xxxxxxx
        c[0] = cp & 0b01111111;

        return 1;
    }
    else if (cp <= 0x000007FF)
    {
        // 110xxxxx
        c[0] = 0b11000000 | (cp >> 6 & 0b00011111);
        // 10xxxxxx
        c[1] = 0b10000000 | (cp & 0b00111111);

        return 2;
    }
    else if (cp <= 0x0000FFFF)
    {
        if (0x0000D800 <= cp && cp <= 0x0000DFFF)
        {
            // excluded range
            return 0;
        }
        else
        {
            // 1110xxxx
            c[0] = 0b11100000 | (cp >> 12 & 0b00001111);
            // 10xxxxxx
            c[1] = 0b10000000 | (cp >> 6 & 0b00111111);
            // 10xxxxxx
            c[2] = 0b10000000 | (cp & 0b00111111);

            return 3;
        }
    }
    else if (cp <= 0x0010FFFF)
    {
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

const char * utf8strchr(const char *s, char32_t u)
{
    char32_t r;
    size_t len = std::strlen(s);
    for (size_t i = 0, j = 0; i < len; i += j)
    {
        if (!(j = utf8decode(&s[i], &r, len - i)))
            break;
        if (r == u)
            return &(s[i]);
    }

    return nullptr;
}

TEST_CASE( "valid code points can be encoded", "[utf8]" ) {

    char buf[] = {0, 0, 0, 0};

    SECTION( "valid one byte encoding" ) {
        auto sz = utf8encode(0x0024, buf);

        REQUIRE( sz == 1 );
        REQUIRE( (unsigned char) buf[0] == 0x24 );
        REQUIRE( (unsigned char) buf[1] == 0x00 );
        REQUIRE( (unsigned char) buf[2] == 0x00 );
        REQUIRE( (unsigned char) buf[3] == 0x00 );
    }

    SECTION( "valid two byte encoding" ) {
        auto sz = utf8encode(0x00A2, buf);

        REQUIRE( sz == 2 );
        REQUIRE( (unsigned char) buf[0] == 0xC2 );
        REQUIRE( (unsigned char) buf[1] == 0xA2 );
        REQUIRE( (unsigned char) buf[2] == 0x00 );
        REQUIRE( (unsigned char) buf[3] == 0x00 );
    }

    SECTION( "valid three byte encoding" ) {
        auto sz = utf8encode(0x20AC, buf);

        REQUIRE( sz == 3 );
        REQUIRE( (unsigned char) buf[0] == 0xE2 );
        REQUIRE( (unsigned char) buf[1] == 0x82 );
        REQUIRE( (unsigned char) buf[2] == 0xAC );
        REQUIRE( (unsigned char) buf[3] == 0x00 );
    }

    SECTION( "valid four byte encoding" ) {
        auto sz = utf8encode(0x10348, buf);

        REQUIRE( sz == 4 );
        REQUIRE( (unsigned char) buf[0] == 0xF0 );
        REQUIRE( (unsigned char) buf[1] == 0x90 );
        REQUIRE( (unsigned char) buf[2] == 0x8D );
        REQUIRE( (unsigned char) buf[3] == 0x88 );
    }
}

TEST_CASE( "cannot encode RFC 3629 code points", "[utf8]" ) {
    // 0xD800 - 0xDFFF are excluded in RFC 3629

    char buf[] = {0, 0, 0, 0};

    SECTION( "before range should work" ) {
        auto sz = utf8encode(0xD7FF, buf);

        REQUIRE( sz == 3 );
        REQUIRE( (unsigned char) buf[0] == 0xED );
        REQUIRE( (unsigned char) buf[1] == 0x9F );
        REQUIRE( (unsigned char) buf[2] == 0xBF );
        REQUIRE( (unsigned char) buf[3] == 0x00 );
    }

    SECTION( "beginning of range fails" ) {
        auto sz = utf8encode(0xD800, buf);

        REQUIRE( sz == 0 );
        REQUIRE( (unsigned char) buf[0] == 0x00 );
        REQUIRE( (unsigned char) buf[1] == 0x00 );
        REQUIRE( (unsigned char) buf[2] == 0x00 );
        REQUIRE( (unsigned char) buf[3] == 0x00 );
    }

    SECTION( "in range fails" ) {
        auto sz = utf8encode(0xD805, buf);

        REQUIRE( sz == 0 );
        REQUIRE( (unsigned char) buf[0] == 0x00 );
        REQUIRE( (unsigned char) buf[1] == 0x00 );
        REQUIRE( (unsigned char) buf[2] == 0x00 );
        REQUIRE( (unsigned char) buf[3] == 0x00 );
    }

    SECTION( "end of range fails" ) {
        auto sz = utf8encode(0xDFFF, buf);

        REQUIRE( sz == 0 );
        REQUIRE( (unsigned char) buf[0] == 0x00 );
        REQUIRE( (unsigned char) buf[1] == 0x00 );
        REQUIRE( (unsigned char) buf[2] == 0x00 );
        REQUIRE( (unsigned char) buf[3] == 0x00 );
    }

    SECTION( "after range should work" ) {
        auto sz = utf8encode(0xE000, buf);

        REQUIRE( sz == 3 );
        REQUIRE( (unsigned char) buf[0] == 0xEE );
        REQUIRE( (unsigned char) buf[1] == 0x80 );
        REQUIRE( (unsigned char) buf[2] == 0x80 );
        REQUIRE( (unsigned char) buf[3] == 0x00 );
    }
}

TEST_CASE( "cannot encode invalid code points", "[utf8]" ) {
    char buf[] = {0, 0, 0, 0};

    SECTION( "last point should work" ) {
        auto sz = utf8encode(0x10FFFF, buf);

        REQUIRE( sz == 4 );
        REQUIRE( (unsigned char) buf[0] == 0xF4 );
        REQUIRE( (unsigned char) buf[1] == 0x8F );
        REQUIRE( (unsigned char) buf[2] == 0xBF );
        REQUIRE( (unsigned char) buf[3] == 0xBF );
    }

    SECTION( "past last point should not work" ) {
        auto sz = utf8encode(0x110000, buf);

        REQUIRE( sz == 0 );
        REQUIRE( (unsigned char) buf[0] == 0x00 );
        REQUIRE( (unsigned char) buf[1] == 0x00 );
        REQUIRE( (unsigned char) buf[2] == 0x00 );
        REQUIRE( (unsigned char) buf[3] == 0x00 );
    }

    SECTION( "way too large should not work" ) {
        auto sz = utf8encode(0x222222, buf);

        REQUIRE( sz == 0 );
        REQUIRE( (unsigned char) buf[0] == 0x00 );
        REQUIRE( (unsigned char) buf[1] == 0x00 );
        REQUIRE( (unsigned char) buf[2] == 0x00 );
        REQUIRE( (unsigned char) buf[3] == 0x00 );
    }
}

TEST_CASE( "valid code points can be decoded", "[utf8]" ) {

    char32_t cp = 0;

    SECTION( "valid one byte encoding, exact len" ) {
        char buf[] = {0x24};
        auto sz = utf8decode(buf, &cp, 1);

        REQUIRE( sz == 1 );
        REQUIRE( cp == 0x24 );
    }

    SECTION( "valid one byte encoding, longer buffer" ) {
        char buf[] = {0x24, 0x24, 0x24, 0x24};
        auto sz = utf8decode(buf, &cp, 4);

        REQUIRE( sz == 1 );
        REQUIRE( cp == 0x24 );
    }

    SECTION( "valid two byte encoding, exact len" ) {
        unsigned char buf[] = {0xC2, 0xA2};
        auto sz = utf8decode((const char *) &buf[0], &cp, 2);

        REQUIRE( sz == 2 );
        REQUIRE( cp == 0xA2 );
    }

    SECTION( "valid two byte encoding, longer buffer" ) {
        unsigned char buf[] = {0xC2, 0xA2, 0x24, 0x24};
        auto sz = utf8decode((const char *) &buf[0], &cp, 4);

        REQUIRE( sz == 2 );
        REQUIRE( cp == 0xA2 );
    }

    SECTION( "valid three byte encoding, exact len" ) {
        unsigned char buf[] = {0xE2, 0x82, 0xAC};
        auto sz = utf8decode((const char *) &buf[0], &cp, 3);

        REQUIRE( sz == 3 );
        REQUIRE( cp == 0x20AC );
    }

    SECTION( "valid three byte encoding, longer buffer" ) {
        unsigned char buf[] = {0xE2, 0x82, 0xAC, 0x24, 0x24};
        auto sz = utf8decode((const char *) &buf[0], &cp, 5);

        REQUIRE( sz == 3 );
        REQUIRE( cp == 0x20AC );
    }

    SECTION( "valid four byte encoding, exact len" ) {
        unsigned char buf[] = {0xF0, 0x90, 0x8D, 0x88};
        auto sz = utf8decode((const char *) &buf[0], &cp, 4);

        REQUIRE( sz == 4 );
        REQUIRE( cp == 0x10348 );
    }

    SECTION( "valid four byte encoding, longer buffer" ) {
        unsigned char buf[] = {0xF0, 0x90, 0x8D, 0x88, 0x24, 0x24};
        auto sz = utf8decode((const char *) &buf[0], &cp, 6);

        REQUIRE( sz == 4 );
        REQUIRE( cp == 0x10348 );
    }
}

/*
TODO:
first of a length
0, 0x80, 0x800, 0x10000
last of a length
0x7F, 0x7FF, 0xFFFF, 0x10FFFF

first continuation byte
0x80
last continuation byte
0xBF
2 continuation bytes
0x80 0xBF
3
0x80 0xBF 0x80
4
0x80 0xBF 0x80 0xBF

0xxxxxxx  0x00..0x7F   Only byte of a 1-byte character encoding
10xxxxxx  0x80..0xBF   Continuation bytes (1-3 continuation bytes)
110xxxxx  0xC0..0xDF   First byte of a 2-byte character encoding
1110xxxx  0xE0..0xEF   First byte of a 3-byte character encoding
11110xxx  0xF0..0xF4   First byte of a 4-byte character encoding
All 32 first bytes of 2-byte sequences (0xc0-0xdf),
       each followed by a space character
3 byte first bytes
All 8 first bytes of 4-byte sequences (0xf0-0xf7),
       each followed by a space character

each length with last byte missing
concatenated string of above
impossible bytes: 0xF5..0xFF

Continuation byte not preceded by one of the initial byte values
Multi-character initial bytes not followed by enough continuation bytes
Non-minimal multi-byte characters
UTF-16 surrogates
Invalid bytes (0xC0, 0xC1, 0xF5..0xFF)
*/

/*
TEST_CASE( "dump", "[utf8]" ) {
    for (int entry = 0; entry < 8; entry++)
    {
        for (int row = 0; row < 16; row++)
        {
            for (int col = 0; col < 16; col++)
            {
                printf("%02X ", UTF8_TRANSITIONS[entry][row * 16 + col]);
            }
            printf("\n");
        }
        printf("\n");
    }
}
*/
