
#include "rwte/selection.h"

bool Selection::selected(int col, int row) const
{
    if (mode == SEL_EMPTY)
        return false;

    if (type == SEL_RECTANGULAR)
        return (nb.row <= row && row <= ne.row) &&
                (nb.col <= col && col <= ne.col);

    return (nb.row <= row && row <= ne.row) &&
            (row != nb.row || col >= nb.col) &&
            (row != ne.row || col <= ne.col);
}
