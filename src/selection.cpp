#include "rwte/selection.h"

#include <cstring>

Selection::Selection() :
	nb{0,0},
	ne{0,0},
	ob{-1,0},
	oe{0,0}
{
    std::memset(&tclick1, 0, sizeof(tclick1));
    std::memset(&tclick2, 0, sizeof(tclick2));
}

void Selection::clear()
{
    m_mode = Mode::Idle;
    m_rectangular = false;
    ob.col = -1;
}

void Selection::begin(int col, int row)
{
    m_mode = Mode::Empty;
    m_rectangular = false;
    oe.col = ob.col = col;
    oe.row = ob.row = row;
}

bool Selection::empty() const
{
    return m_mode == Mode::Empty || ob.col == -1;
}

bool Selection::selected(int col, int row) const
{
    if (empty())
        return false;

    if (m_rectangular)
        return (nb.row <= row && row <= ne.row) &&
                (nb.col <= col && col <= ne.col);

    return (nb.row <= row && row <= ne.row) &&
            (row != nb.row || col >= nb.col) &&
            (row != ne.row || col <= ne.col);
}
