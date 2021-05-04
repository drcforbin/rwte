#ifndef RWTE_EVENT_H
#define RWTE_EVENT_H

#include "rw/bus.h"

namespace event {

struct Resize
{
    size_t width, height;
    int cols, rows;
};

struct Refresh
{};

typedef Bus<
        Resize,
        Refresh>
        Bus;

} // namespace event

#endif // RWTE_EVENT_H
