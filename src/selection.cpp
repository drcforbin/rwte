#include <cstring>

#include "rwte/selection.h"

Selection::Selection() :
    mode(SEL_IDLE),
	type(0),
	snap(0),
	nb{0,0},
	ne{0,0},
	ob{-1,0},
	oe{0,0},
	alt(false)
{
    std::memset(&tclick1, 0, sizeof(tclick1));
    std::memset(&tclick2, 0, sizeof(tclick2));
}

void Selection::clear()
{
    mode = SEL_IDLE;
    ob.col = -1;
}

bool Selection::empty() const
{
    return ob.col == -1;
}

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
