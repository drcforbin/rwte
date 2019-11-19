#include "doctest.h"
#include "rwte/utf8.h"

#include <array>
#include <cstring>
#include <string_view>
#include <tuple>
#include <vector>

using namespace std::literals;

extern const char* unicode_text;

// todo: test utf8contains

TEST_SUITE_BEGIN("utf8");

TEST_CASE("valid code points can be encoded")
{
    std::array<char, 4> buf;

    SUBCASE("valid one byte encoding")
    {
        auto end = utf8encode(0x0024, buf.begin());

        REQUIRE(end - buf.begin() == 1);
        REQUIRE((unsigned char) buf[0] == 0x24);
    }

    SUBCASE("valid two byte encoding")
    {
        auto end = utf8encode(0x00A2, buf.begin());

        REQUIRE(end - buf.begin() == 2);
        REQUIRE((unsigned char) buf[0] == 0xC2);
        REQUIRE((unsigned char) buf[1] == 0xA2);
    }

    SUBCASE("valid three byte encoding")
    {
        auto end = utf8encode(0x20AC, buf.begin());

        REQUIRE(end - buf.begin() == 3);
        REQUIRE((unsigned char) buf[0] == 0xE2);
        REQUIRE((unsigned char) buf[1] == 0x82);
        REQUIRE((unsigned char) buf[2] == 0xAC);
    }

    SUBCASE("valid four byte encoding")
    {
        auto end = utf8encode(0x10348, buf.begin());

        REQUIRE(end - buf.begin() == 4);
        REQUIRE((unsigned char) buf[0] == 0xF0);
        REQUIRE((unsigned char) buf[1] == 0x90);
        REQUIRE((unsigned char) buf[2] == 0x8D);
        REQUIRE((unsigned char) buf[3] == 0x88);
    }

    SUBCASE("unicode text")
    {
        std::vector<char32_t> chars;
        std::string_view v{unicode_text};
        while (!v.empty()) {
            auto [len, cp] = utf8decode(v);
            chars.push_back(cp);
            v = v.substr(len);
        }

        v = {unicode_text};
        for (auto cp : chars) {
            buf.fill(0);
            auto end = utf8encode(cp, buf.begin());
            auto len = end - buf.begin();
            REQUIRE(len > 0);
            bool nonzero = buf[0] || buf[1] || buf[2] || buf[3];
            REQUIRE(nonzero);

            for (decltype(len) i = 0; i < len; i++) {
                REQUIRE(v[i] == buf[i]);
            }
            v = v.substr(len);
        }
    }
}

TEST_CASE("cannot encode RFC 3629 code points")
{
    // 0xD800 - 0xDFFF are excluded in RFC 3629

    std::array<char, 4> buf;

    SUBCASE("before range should work")
    {
        auto end = utf8encode(0xD7FF, buf.begin());

        REQUIRE(end - buf.begin() == 3);
        REQUIRE((unsigned char) buf[0] == 0xED);
        REQUIRE((unsigned char) buf[1] == 0x9F);
        REQUIRE((unsigned char) buf[2] == 0xBF);
    }

    SUBCASE("beginning of range fails")
    {
        auto end = utf8encode(0xD800, buf.begin());
        REQUIRE(end == buf.begin());
    }

    SUBCASE("in range fails")
    {
        auto end = utf8encode(0xD805, buf.begin());
        REQUIRE(end == buf.begin());
    }

    SUBCASE("end of range fails")
    {
        auto end = utf8encode(0xDFFF, buf.begin());
        REQUIRE(end == buf.begin());
    }

    SUBCASE("after range should work")
    {
        auto end = utf8encode(0xE000, buf.begin());

        REQUIRE(end - buf.begin() == 3);
        REQUIRE((unsigned char) buf[0] == 0xEE);
        REQUIRE((unsigned char) buf[1] == 0x80);
        REQUIRE((unsigned char) buf[2] == 0x80);
    }
}

TEST_CASE("cannot encode invalid code points")
{
    std::array<char, 4> buf;

    SUBCASE("last point should work")
    {
        auto end = utf8encode(0x10FFFF, buf.begin());

        REQUIRE(end - buf.begin() == 4);
        REQUIRE((unsigned char) buf[0] == 0xF4);
        REQUIRE((unsigned char) buf[1] == 0x8F);
        REQUIRE((unsigned char) buf[2] == 0xBF);
        REQUIRE((unsigned char) buf[3] == 0xBF);
    }

    SUBCASE("past last point should not work")
    {
        auto end = utf8encode(0x110000, buf.begin());
        REQUIRE(end == buf.begin());
    }

    SUBCASE("way too large should not work")
    {
        auto end = utf8encode(0x222222, buf.begin());
        REQUIRE(end == buf.begin());
    }
}

TEST_CASE("valid code points can be decoded")
{
    SUBCASE("valid one byte encoding, exact len")
    {
        auto v = "\x24"sv;
        auto [sz, cp] = utf8decode(v);

        REQUIRE(sz == 1);
        REQUIRE(cp == 0x24);
    }

    SUBCASE("valid one byte encoding, longer buffer")
    {
        auto v = "\x24\x24\x24\x24"sv;
        auto [sz, cp] = utf8decode(v);

        REQUIRE(sz == 1);
        REQUIRE(cp == 0x24);
    }

    SUBCASE("valid two byte encoding, exact len")
    {
        auto v = "\xC2\xA2"sv;
        auto [sz, cp] = utf8decode(v);

        REQUIRE(sz == 2);
        REQUIRE(cp == 0xA2);
    }

    SUBCASE("valid two byte encoding, longer buffer")
    {
        auto v = "\xC2\xA2\x24\x24"sv;
        auto [sz, cp] = utf8decode(v);

        REQUIRE(sz == 2);
        REQUIRE(cp == 0xA2);
    }

    SUBCASE("valid three byte encoding, exact len")
    {
        auto v = "\xE2\x82\xAC"sv;
        auto [sz, cp] = utf8decode(v);

        REQUIRE(sz == 3);
        REQUIRE(cp == 0x20AC);
    }

    SUBCASE("valid three byte encoding, longer buffer")
    {
        auto v = "\xE2\x82\xAC\x24\x24"sv;
        auto [sz, cp] = utf8decode(v);

        REQUIRE(sz == 3);
        REQUIRE(cp == 0x20AC);
    }

    SUBCASE("valid four byte encoding, exact len")
    {
        auto v = "\xF0\x90\x8D\x88"sv;
        auto [sz, cp] = utf8decode(v);

        REQUIRE(sz == 4);
        REQUIRE(cp == 0x10348);
    }

    SUBCASE("valid four byte encoding, longer buffer")
    {
        auto v = "\xF0\x90\x8D\x88\x24\x24"sv;
        auto [sz, cp] = utf8decode(v);

        REQUIRE(sz == 4);
        REQUIRE(cp == 0x10348);
    }

    SUBCASE("unicode text")
    {
        std::string_view v{unicode_text};
        auto [sz, cp] = utf8decode(v);
        while (v.size() > 0 && sz > 0) {
            REQUIRE(sz > 0);
            REQUIRE(cp > 0);
            v = v.substr(sz);
            std::tie(sz, cp) = utf8decode(v);
        }
        REQUIRE(v.size() == 0);
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
TEST_CASE( "dump" ) {
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

TEST_SUITE_END();
