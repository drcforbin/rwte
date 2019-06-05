#ifndef RWTE_EVENT_H
#define RWTE_EVENT_H

#include "rwte/bus.h"
#include <memory>

class SetPrintModeEvt {};

struct ResizeEvt
{
    size_t width, height;
    int cols, rows;
};

typedef Bus<
    SetPrintModeEvt,
    ResizeEvt
> RwteBus;

#endif // RWTE_EVENT_H
