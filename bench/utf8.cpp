#include "nanobench.h"
#include "rwte/utf8.h"

#include <array>
#include <codecvt>
#include <cuchar>
#include <locale>
#include <tuple>
#include <random>
#include <string_view>

using namespace std::literals;

extern const char* unicode_text;

void bench_utf8_encoding(ankerl::nanobench::Config& cfg)
{
    std::vector<char32_t> chars;
    std::string_view v{unicode_text};
    while (!v.empty()) {
        auto [len, cp] = utf8decode(v);
        chars.push_back(cp);
        v = v.substr(len);
    }

    int sum = 0;
    cfg.minEpochIterations(10).run("utf8 encoding", [&] {
        v = {unicode_text};
        for (auto cp : chars) {
            std::array<char, 4> buf;
            auto end = utf8encode(cp, buf.begin());
            auto len = end - buf.begin();
            v = v.substr(len);
            sum += buf[0] + buf[1] + buf[3] + buf[4];
        }
    }).doNotOptimizeAway(&sum);
}

void bench_utf8_decoding(ankerl::nanobench::Config& cfg)
{
    int sum = 0;
    std::string_view v{unicode_text};
    cfg.minEpochIterations(40).run("utf8 decoding text", [&] {
        auto [sz, cp] = utf8decode(v);
        while (!v.empty() && sz > 0) {
            v = v.substr(sz);
            std::tie(sz, cp) = utf8decode(v);
            sum += cp - sz;
        }
    }).doNotOptimizeAway(&sum);

    sum = 0;
    cfg.minEpochIterations(40).run("utf8 decoding valid chars", [&] {
        auto v = "\x24"sv;
        auto [sz, cp] = utf8decode(v);
        sum += cp - sz;

        v = "\x24\x24\x24\x24"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;

        v = "\xC2\xA2"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;

        v = "\xC2\xA2\x24\x24"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;

        v = "\xE2\x82\xAC"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;

        v = "\xE2\x82\xAC\x24\x24"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;

        v = "\xF0\x90\x8D\x88"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;

        v = "\xF0\x90\x8D\x88\x24\x24"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;
    }).doNotOptimizeAway(&sum);


    sum = 0;
    cfg.minEpochIterations(40).run("utf8 decoding valid chars 2", [&] {
        auto utf8decode = [&](std::string_view c) ->
            std::pair<std::size_t, char32_t> {
            std::mbstate_t mb = std::mbstate_t();
            char32_t c32;
            std::size_t rc = std::mbrtoc32(&c32, c.data(), c.size(), &mb);
            return {rc, c32};
        };

        auto v = "\x24"sv;
        auto [sz, cp] = utf8decode(v);
        sum += cp - sz;

        v = "\x24\x24\x24\x24"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;

        v = "\xC2\xA2"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;

        v = "\xC2\xA2\x24\x24"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;

        v = "\xE2\x82\xAC"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;

        v = "\xE2\x82\xAC\x24\x24"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;

        v = "\xF0\x90\x8D\x88"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;

        v = "\xF0\x90\x8D\x88\x24\x24"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;
    }).doNotOptimizeAway(&sum);

    sum = 0;
    cfg.minEpochIterations(40).run("utf8 decoding valid chars 3", [&] {
        auto utf8decode = [&](std::string_view c) ->
            std::pair<std::size_t, char32_t> {
            std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> cvt;
            std::u32string utf32 = cvt.from_bytes(c.data(), c.data()+c.size());
            return {cvt.converted(), utf32[0]};
        };

        auto v = "\x24"sv;
        auto [sz, cp] = utf8decode(v);
        sum += cp - sz;

        v = "\x24\x24\x24\x24"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;

        v = "\xC2\xA2"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;

        v = "\xC2\xA2\x24\x24"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;

        v = "\xE2\x82\xAC"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;

        v = "\xE2\x82\xAC\x24\x24"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;

        v = "\xF0\x90\x8D\x88"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;

        v = "\xF0\x90\x8D\x88\x24\x24"sv;
        std::tie(sz, cp) = utf8decode(v);
        sum += cp - sz;
    }).doNotOptimizeAway(&sum);
}

