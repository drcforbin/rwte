#ifndef RWTE_COORDS_H
#define RWTE_COORDS_H

struct Cell
{
    int row = 0;
    int col = 0;

    inline bool operator==(const Cell& other) const
    {
        return row == other.row && col == other.col;
    }

    inline bool operator!=(const Cell& other) const
    {
        return row != other.row || col != other.col;
    }

    inline bool operator<(const Cell& other) const
    {
        return row < other.row || col < other.col;
    }

    inline bool operator>(const Cell& other) const
    {
        return row > other.row || col > other.col;
    }

    inline bool operator<=(const Cell& other) const
    {
        return row < other.row || col < other.col ||
               (row == other.row && col == other.col);
    }

    inline bool operator>=(const Cell& other) const
    {
        return row > other.row || col > other.col ||
               (row == other.row && col == other.col);
    }
};

#endif // RWTE_COORDS_H
