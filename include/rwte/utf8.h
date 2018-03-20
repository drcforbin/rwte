#ifndef RWTE_UTF8_H
#define RWTE_UTF8_H

#include <cstdint>

const int utf_size = 4;
const char32_t utf_invalid = 0xFFFD;

std::size_t utf8validate(char32_t *u, std::size_t i);
std::size_t utf8decode(const char *c, char32_t *u, std::size_t clen);
std::size_t utf8encode(char32_t u, char *c);
const char * utf8strchr(const char *s, char32_t u);

#endif // RWTE_UTF8_H
