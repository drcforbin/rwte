#define ANKERL_NANOBENCH_IMPLEMENT
#include "nanobench.h"

#include "rw/logging.h"

// todo: better way than extern.

void bench_string_cmp(ankerl::nanobench::Config& cfg);
void bench_utf8_encoding(ankerl::nanobench::Config& cfg);
void bench_utf8_decoding(ankerl::nanobench::Config& cfg);

int main()
{
    auto cfg = ankerl::nanobench::Config();

    bench_string_cmp(cfg);
    bench_utf8_encoding(cfg);
    bench_utf8_decoding(cfg);
}
