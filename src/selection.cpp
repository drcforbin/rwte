#include "rwte/selection.h"

#include <cstring>

Selection::Selection()
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

void Selection::begin(const Cell& cell)
{
    m_mode = Mode::Empty;
    m_rectangular = false;
    oe = ob = cell;
}

bool Selection::empty() const
{
    return m_mode == Mode::Empty || ob.col == -1;
}

bool Selection::anyselected(const Cell& begin, const Cell& end) const
{
    if (empty())
        return false;

    // todo: make this not dumb
    Cell cell;
    for (cell.row = begin.row; cell.row <= end.row; cell.row++)
        for (cell.col = begin.col; cell.col <= end.col; cell.col++)
            if (selected(cell))
                return true;

    return false;
}

bool Selection::selected(const Cell& cell) const
{
    if (empty())
        return false;

    if (m_rectangular)
        return (nb.row <= cell.row && cell.row <= ne.row) &&
                (nb.col <= cell.col && cell.col <= ne.col);

    return (nb.row <= cell.row && cell.row <= ne.row) &&
            (cell.row != nb.row || cell.col >= nb.col) &&
            (cell.row != ne.row || cell.col <= ne.col);
}
