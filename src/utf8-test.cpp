#include "rwte/catch.hpp"
#include "rwte/utf8.h"

#include <array>
#include <cstring>

TEST_CASE("valid code points can be encoded", "[utf8]")
{
    char buf[] = {0, 0, 0, 0};

    SECTION("valid one byte encoding")
    {
        auto sz = utf8encode(0x0024, buf);

        REQUIRE(sz == 1);
        REQUIRE((unsigned char) buf[0] == 0x24);
        REQUIRE((unsigned char) buf[1] == 0x00);
        REQUIRE((unsigned char) buf[2] == 0x00);
        REQUIRE((unsigned char) buf[3] == 0x00);
    }

    SECTION("valid two byte encoding")
    {
        auto sz = utf8encode(0x00A2, buf);

        REQUIRE(sz == 2);
        REQUIRE((unsigned char) buf[0] == 0xC2);
        REQUIRE((unsigned char) buf[1] == 0xA2);
        REQUIRE((unsigned char) buf[2] == 0x00);
        REQUIRE((unsigned char) buf[3] == 0x00);
    }

    SECTION("valid three byte encoding")
    {
        auto sz = utf8encode(0x20AC, buf);

        REQUIRE(sz == 3);
        REQUIRE((unsigned char) buf[0] == 0xE2);
        REQUIRE((unsigned char) buf[1] == 0x82);
        REQUIRE((unsigned char) buf[2] == 0xAC);
        REQUIRE((unsigned char) buf[3] == 0x00);
    }

    SECTION("valid four byte encoding")
    {
        auto sz = utf8encode(0x10348, buf);

        REQUIRE(sz == 4);
        REQUIRE((unsigned char) buf[0] == 0xF0);
        REQUIRE((unsigned char) buf[1] == 0x90);
        REQUIRE((unsigned char) buf[2] == 0x8D);
        REQUIRE((unsigned char) buf[3] == 0x88);
    }
}

TEST_CASE("cannot encode RFC 3629 code points", "[utf8]")
{
    // 0xD800 - 0xDFFF are excluded in RFC 3629

    char buf[] = {0, 0, 0, 0};

    SECTION("before range should work")
    {
        auto sz = utf8encode(0xD7FF, buf);

        REQUIRE(sz == 3);
        REQUIRE((unsigned char) buf[0] == 0xED);
        REQUIRE((unsigned char) buf[1] == 0x9F);
        REQUIRE((unsigned char) buf[2] == 0xBF);
        REQUIRE((unsigned char) buf[3] == 0x00);
    }

    SECTION("beginning of range fails")
    {
        auto sz = utf8encode(0xD800, buf);

        REQUIRE(sz == 0);
        REQUIRE((unsigned char) buf[0] == 0x00);
        REQUIRE((unsigned char) buf[1] == 0x00);
        REQUIRE((unsigned char) buf[2] == 0x00);
        REQUIRE((unsigned char) buf[3] == 0x00);
    }

    SECTION("in range fails")
    {
        auto sz = utf8encode(0xD805, buf);

        REQUIRE(sz == 0);
        REQUIRE((unsigned char) buf[0] == 0x00);
        REQUIRE((unsigned char) buf[1] == 0x00);
        REQUIRE((unsigned char) buf[2] == 0x00);
        REQUIRE((unsigned char) buf[3] == 0x00);
    }

    SECTION("end of range fails")
    {
        auto sz = utf8encode(0xDFFF, buf);

        REQUIRE(sz == 0);
        REQUIRE((unsigned char) buf[0] == 0x00);
        REQUIRE((unsigned char) buf[1] == 0x00);
        REQUIRE((unsigned char) buf[2] == 0x00);
        REQUIRE((unsigned char) buf[3] == 0x00);
    }

    SECTION("after range should work")
    {
        auto sz = utf8encode(0xE000, buf);

        REQUIRE(sz == 3);
        REQUIRE((unsigned char) buf[0] == 0xEE);
        REQUIRE((unsigned char) buf[1] == 0x80);
        REQUIRE((unsigned char) buf[2] == 0x80);
        REQUIRE((unsigned char) buf[3] == 0x00);
    }
}

TEST_CASE("cannot encode invalid code points", "[utf8]")
{
    char buf[] = {0, 0, 0, 0};

    SECTION("last point should work")
    {
        auto sz = utf8encode(0x10FFFF, buf);

        REQUIRE(sz == 4);
        REQUIRE((unsigned char) buf[0] == 0xF4);
        REQUIRE((unsigned char) buf[1] == 0x8F);
        REQUIRE((unsigned char) buf[2] == 0xBF);
        REQUIRE((unsigned char) buf[3] == 0xBF);
    }

    SECTION("past last point should not work")
    {
        auto sz = utf8encode(0x110000, buf);

        REQUIRE(sz == 0);
        REQUIRE((unsigned char) buf[0] == 0x00);
        REQUIRE((unsigned char) buf[1] == 0x00);
        REQUIRE((unsigned char) buf[2] == 0x00);
        REQUIRE((unsigned char) buf[3] == 0x00);
    }

    SECTION("way too large should not work")
    {
        auto sz = utf8encode(0x222222, buf);

        REQUIRE(sz == 0);
        REQUIRE((unsigned char) buf[0] == 0x00);
        REQUIRE((unsigned char) buf[1] == 0x00);
        REQUIRE((unsigned char) buf[2] == 0x00);
        REQUIRE((unsigned char) buf[3] == 0x00);
    }
}

TEST_CASE("valid code points can be decoded", "[utf8]")
{
    char32_t cp = 0;

    SECTION("valid one byte encoding, exact len")
    {
        char buf[] = {0x24};
        auto sz = utf8decode(buf, &cp, 1);

        REQUIRE(sz == 1);
        REQUIRE(cp == 0x24);
    }

    SECTION("valid one byte encoding, longer buffer")
    {
        char buf[] = {0x24, 0x24, 0x24, 0x24};
        auto sz = utf8decode(buf, &cp, 4);

        REQUIRE(sz == 1);
        REQUIRE(cp == 0x24);
    }

    SECTION("valid two byte encoding, exact len")
    {
        unsigned char buf[] = {0xC2, 0xA2};
        auto sz = utf8decode((const char*) &buf[0], &cp, 2);

        REQUIRE(sz == 2);
        REQUIRE(cp == 0xA2);
    }

    SECTION("valid two byte encoding, longer buffer")
    {
        unsigned char buf[] = {0xC2, 0xA2, 0x24, 0x24};
        auto sz = utf8decode((const char*) &buf[0], &cp, 4);

        REQUIRE(sz == 2);
        REQUIRE(cp == 0xA2);
    }

    SECTION("valid three byte encoding, exact len")
    {
        unsigned char buf[] = {0xE2, 0x82, 0xAC};
        auto sz = utf8decode((const char*) &buf[0], &cp, 3);

        REQUIRE(sz == 3);
        REQUIRE(cp == 0x20AC);
    }

    SECTION("valid three byte encoding, longer buffer")
    {
        unsigned char buf[] = {0xE2, 0x82, 0xAC, 0x24, 0x24};
        auto sz = utf8decode((const char*) &buf[0], &cp, 5);

        REQUIRE(sz == 3);
        REQUIRE(cp == 0x20AC);
    }

    SECTION("valid four byte encoding, exact len")
    {
        unsigned char buf[] = {0xF0, 0x90, 0x8D, 0x88};
        auto sz = utf8decode((const char*) &buf[0], &cp, 4);

        REQUIRE(sz == 4);
        REQUIRE(cp == 0x10348);
    }

    SECTION("valid four byte encoding, longer buffer")
    {
        unsigned char buf[] = {0xF0, 0x90, 0x8D, 0x88, 0x24, 0x24};
        auto sz = utf8decode((const char*) &buf[0], &cp, 6);

        REQUIRE(sz == 4);
        REQUIRE(cp == 0x10348);
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

