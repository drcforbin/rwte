
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include "rw/logging.h"
#include "rw/utf8.h"

#define LOGGER() (rw::logging::get("rwte-bench"))

int main(int argc, char** argv)
{
    rw::utf8::set_locale();

    return doctest::Context(argc, argv).run();
}
