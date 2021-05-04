#ifndef RWTE_COLOR_H
#define RWTE_COLOR_H

namespace color {

constexpr bool isTruecol(uint32_t x)
{
    return 1 << 24 & x;
}

template <typename T,
        typename = typename std::enable_if<std::is_integral<T>::value, T>::type>
constexpr uint32_t truecol(T r, T g, T b)
{
    return 1 << 24 | (r & 0xFF) << 16 | (g & 0xFF) << 8 | (b & 0xFF);
}

constexpr uint8_t redByte(uint32_t c)
{
    return (c & 0xFF0000) >> 16;
}

constexpr uint8_t greenByte(uint32_t c)
{
    return (c & 0x00FF00) >> 8;
}

constexpr uint8_t blueByte(uint32_t c)
{
    return c & 0x0000FF;
}

} // namespace color

#endif // RWTE_COLOR_H
