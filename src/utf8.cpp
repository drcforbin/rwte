#include "rwte/term.h"
#include "rwte/utf8.h"

#include <cstring>

#define LEN(a) (sizeof(a) / sizeof(a)[0])

static const unsigned char utfbyte[utf_size + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0};
static const unsigned char utfmask[utf_size + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
static const Rune utfmin[utf_size + 1] = {       0,    0,  0x80,  0x800,  0x10000};
static const Rune utfmax[utf_size + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

std::size_t utf8validate(Rune *u, std::size_t i)
{
    if (*u < utfmin[i] || utfmax[i] < *u || (0xD800 <= *u && *u <= 0xDFFF))
        *u = utf_invalid;
    for (i = 1; *u > utfmax[i]; ++i) {}

    return i;
}

static Rune utf8decodebyte(char c, std::size_t *i)
{
    for (*i = 0; *i < LEN(utfmask); ++(*i))
        if (((unsigned char)c & utfmask[*i]) == utfbyte[*i])
            return (unsigned char)c & ~utfmask[*i];

    return 0;
}

std::size_t utf8decode(const char *c, Rune *u, std::size_t clen)
{
    *u = utf_invalid;
    if (!clen)
        return 0;

    std::size_t len;
    Rune udecoded = utf8decodebyte(c[0], &len);
    if (len < 1 || utf_size < len)
        return 1;

    int j = 1;
    for (int i = 1; i < clen && j < len; ++i, ++j)
    {
        std::size_t type;
        udecoded = (udecoded << 6) | utf8decodebyte(c[i], &type);
        if (type != 0)
            return j;
    }

    if (j < len)
        return 0;

    *u = udecoded;
    utf8validate(u, len);

    return len;
}

static char utf8encodebyte(Rune u, size_t i)
{
    return utfbyte[i] | (u & ~utfmask[i]);
}

size_t utf8encode(Rune u, char *c)
{
    size_t len = utf8validate(&u, 0);
    if (len > utf_size)
        return 0;

    for (size_t i = len - 1; i != 0; --i)
    {
        c[i] = utf8encodebyte(u, 0);
        u >>= 6;
    }
    c[0] = utf8encodebyte(u, len);

    return len;
}

const char * utf8strchr(const char *s, Rune u)
{
    Rune r;
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
