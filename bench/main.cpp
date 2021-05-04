#define ANKERL_NANOBENCH_IMPLEMENT
#include "nanobench.h"

#include "rw/logging.h"
#include "rw/utf8.h"

#define LOGGER() (rw::logging::get("rwte-bench"))

// todo: better way than extern.

void bench_string_cmp(ankerl::nanobench::Config& cfg);

int main()
{
    rw::utf8::set_locale();

    auto cfg = ankerl::nanobench::Config();

    bench_string_cmp(cfg);
}
