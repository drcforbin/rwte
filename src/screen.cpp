#include "lua/config.h"
#include "lua/state.h"
#include "rwte/logging.h"
#include "rwte/rwte.h"
#include "rwte/screen.h"
#include "rwte/selection.h"
#include "rwte/utf8.h"

#define LOGGER() (logging::get("screen"))

namespace screen {

template <typename T,
        typename = typename std::enable_if<std::is_arithmetic<T>::value, T>::type>
constexpr T limit(T x, T a, T b)
{
    return x < a ? a : (x > b ? b : x);
}

static cursor_type get_cursor_type()
{
    auto cursor_type = lua::config::get_string("cursor_type");

    if (cursor_type == "blink block")
        return cursor_type::CURSOR_BLINK_BLOCK;
    else if (cursor_type == "steady block")
        return cursor_type::CURSOR_STEADY_BLOCK;
    else if (cursor_type == "blink under")
        return cursor_type::CURSOR_BLINK_UNDER;
    else if (cursor_type == "steady under")
        return cursor_type::CURSOR_STEADY_UNDER;
    else if (cursor_type == "blink bar")
        return cursor_type::CURSOR_BLINK_BAR;
    else if (cursor_type == "steady bar")
        return cursor_type::CURSOR_STEADY_BAR;
    else
        return cursor_type::CURSOR_STEADY_BLOCK;
}

static bool isdelim(char32_t c)
{
    auto L = rwte->lua();
    L->getglobal("config");
    L->getfield(-1, "word_delimiters");
    const char* word_delimiters = L->tostring(-1);

    // if word_delimiters is missing, it'll select whole line
    // TODO: look to replacing utf8strchr with wcschr
    // (that's what st did)
    // their default word_delimiters is just " " by default
    bool delim = false;
    if (word_delimiters)
        delim = c != 0 && utf8strchr(word_delimiters, c) != nullptr;

    L->pop(2);
    return delim;
}

class ScreenImpl
{
public:
    ScreenImpl(std::shared_ptr<event::Bus> bus) :
        m_bus(bus),
        m_rows(0),
        m_cols(0),
        m_top(0),
        m_bot(0)
    {}

    void reset()
    {
        m_cursortype = get_cursor_type();

        m_top = 0;
        m_bot = m_rows - 1;

        for (int i = 0; i < 2; i++) {
            clear();
            swapscreen();
        }
    }

    void resize(int cols, int rows)
    {
        // slide screen to keep cursor where we expect it
        if (m_cursor.row - rows >= 0) {
            LOGGER()->debug("cursor {}, {}", m_cursor.row, m_cursor.col);
            LOGGER()->debug("removing {} lines for cursor",
                    (m_cursor.row - rows) + 1);
            m_lines.erase(m_lines.cbegin(),
                    m_lines.cbegin() + (m_cursor.row - rows) + 1);
            m_alt_lines.erase(m_alt_lines.cbegin(),
                    m_alt_lines.cbegin() + (m_cursor.row - rows) + 1);
        }

        // resize to new height
        m_lines.resize(rows);
        m_alt_lines.resize(rows);
        m_dirty.resize(rows);

        // resize each row to new width, zero-pad if needed
        int i;
        int minrow = std::min(rows, m_rows);
        for (i = 0; i < minrow; i++) {
            m_lines[i].resize(cols);
            m_alt_lines[i].resize(cols);
        }

        // allocate any new rows
        for (; i < rows; i++) {
            m_lines[i].resize(cols);
            m_alt_lines[i].resize(cols);
        }

        // update terminal size
        m_cols = cols;
        m_rows = rows;
    }

    void swapscreen()
    {
        std::swap(m_lines, m_alt_lines);
        setdirty();
    }

    void clear()
    {
        clear({0, 0}, {m_rows - 1, m_cols - 1});
    }

    // note: includes end
    void clear(const Cell& begin, const Cell& end)
    {
        int col1 = limit(begin.col, 0, m_cols - 1);
        int row1 = limit(begin.row, 0, m_rows - 1);
        int col2 = limit(end.col, 0, m_cols - 1);
        int row2 = limit(end.row, 0, m_rows - 1);

        if (col1 > col2)
            std::swap(col1, col2);
        if (row1 > row2)
            std::swap(row1, row2);

        Glyph empty{
                empty_char,
                {},
                m_cursor.attr.fg,
                m_cursor.attr.bg};

        fill({row1, col1}, {row2, col2}, empty);

        if (m_sel.anyselected({row1, col1}, {row2, col2}))
            selclear();
    }

    const glyph_attribute& attr(const Cell& cell) const
    {
        return glyph(cell).attr;
    }

    Glyph& glyph(const Cell& cell)
    {
        return m_lines[cell.row][cell.col];
    }

    const Glyph& glyph(const Cell& cell) const
    {
        return m_lines[cell.row][cell.col];
    }

    void setGlyph(const Cell& cell, const Glyph& glyph)
    {
        m_lines[cell.row][cell.col] = glyph;
        m_dirty[cell.row] = true;

        m_bus->publish(event::Refresh{});
    }

    void newline(bool first_col)
    {
        int row = m_cursor.row;

        if (row == m_bot)
            scrollup(m_top, 1);
        else
            row++;

        moveto({row, first_col ? 0 : m_cursor.col});
    }

    void deleteline(int n)
    {
        // todo: work out what to do for delete 0 (currently broken?)
        if (m_top <= m_cursor.row && m_cursor.row <= m_bot)
            scrollup(m_cursor.row, n);
    }

    void insertblankline(int n)
    {
        // todo: work out what to do for delete 0 (currently broken?)
        if (m_top <= m_cursor.row && m_cursor.row <= m_bot)
            scrolldown(m_cursor.row, n);
    }

    void deletechar(int n)
    {
        n = limit(n, 0, m_cols - m_cursor.col);

        int dst = m_cursor.col;
        int src = m_cursor.col + n;
        int size = m_cols - src;

        // move to screen
        auto lineit = m_lines[m_cursor.row].begin();
        std::copy(lineit + src, lineit + src + size, lineit + dst);
        clear({m_cursor.row, m_cols - n}, {m_cursor.row, m_cols - 1});
    }

    void insertblank(int n)
    {
        n = limit(n, 0, m_cols - m_cursor.col);
        if (n > 0) {
            // move things over
            auto& line = m_lines[m_cursor.row];
            std::copy_backward(
                    line.begin() + m_cursor.col,
                    line.end() - n,
                    line.end());

            // clear moved area
            clear(m_cursor, {m_cursor.row, m_cursor.col + n - 1});
        }
    }

    void setscroll(int t, int b)
    {
        t = limit(t, 0, m_rows - 1);
        b = limit(b, 0, m_rows - 1);

        if (t > b)
            std::swap(t, b);

        m_top = t;
        m_bot = b;
    }

    void scrollup(int orig, int n)
    {
        n = limit(n, 0, m_bot - orig + 1);

        clear({orig, 0}, {orig + n - 1, m_cols - 1});
        setdirty(orig + n, m_bot);

        for (int i = orig; i <= m_bot - n; i++)
            std::swap(m_lines[i], m_lines[i + n]);

        selscroll(orig, -n);
    }

    void scrolldown(int orig, int n)
    {
        n = limit(n, 0, m_bot - orig + 1);

        setdirty(orig, m_bot - n);
        clear({m_bot - n + 1, 0}, {m_bot, m_cols - 1});

        for (int i = m_bot; i >= orig + n; i--)
            std::swap(m_lines[i], m_lines[i - n]);

        selscroll(orig, n);
    }

    void moveto(const Cell& cell)
    {
        int minrow, maxrow;
        if (m_cursor.state & CURSOR_ORIGIN) {
            minrow = m_top;
            maxrow = m_bot;
        } else {
            minrow = 0;
            maxrow = m_rows - 1;
        }

        m_cursor.state &= ~CURSOR_WRAPNEXT;
        m_cursor.col = limit(cell.col, 0, m_cols - 1);
        m_cursor.row = limit(cell.row, minrow, maxrow);

        m_bus->publish(event::Refresh{});
    }

    // for absolute user moves, when decom is set
    void moveato(const Cell& cell)
    {
        moveto({cell.row + ((m_cursor.state & CURSOR_ORIGIN) ? m_top : 0),
                cell.col});
    }

    void selsnap(int* col, int* row, int direction)
    {
        int newcol, newrow, colt, rowt;
        int delim, prevdelim;
        Glyph *gp, *prevgp;

        switch (m_sel.snap) {
            case Selection::Snap::Word:
                // Snap around if the word wraps around at the end or
                // beginning of a line.

                prevgp = &m_lines[*row][*col];
                prevdelim = isdelim(prevgp->u);
                for (;;) {
                    newcol = *col + direction;
                    newrow = *row;
                    if (newcol < 0 || (m_cols - 1) < newcol) {
                        newrow += direction;
                        newcol = (newcol + m_cols) % m_cols;
                        if (newrow < 0 || (m_rows - 1) < newrow)
                            break;

                        if (direction > 0)
                            rowt = *row, colt = *col;
                        else
                            rowt = newrow, colt = newcol;
                        if (!attr({rowt, colt})[ATTR_WRAP])
                            break;
                    }

                    if (newcol >= linelen(newrow))
                        break;

                    gp = &m_lines[newrow][newcol];
                    delim = isdelim(gp->u);
                    if (!gp->attr[ATTR_WDUMMY] &&
                            (delim != prevdelim || (delim && gp->u != prevgp->u)))
                        break;

                    *col = newcol;
                    *row = newrow;
                    prevgp = gp;
                    prevdelim = delim;
                }
                break;
            case Selection::Snap::Line:
                // Snap around if the the previous line or the current one
                // has set ATTR_WRAP at its end. Then the whole next or
                // previous line will be selected.

                *col = (direction < 0) ? 0 : m_cols - 1;
                if (direction < 0) {
                    for (; *row > 0; *row += direction) {
                        if (!attr({*row - 1, m_cols - 1})[ATTR_WRAP]) {
                            break;
                        }
                    }
                } else if (direction > 0) {
                    for (; *row < m_rows - 1; *row += direction) {
                        if (!attr({*row, m_cols - 1})[ATTR_WRAP]) {
                            break;
                        }
                    }
                }
                break;
            default:
                // noop
                break;
        }
    }

    void selclear()
    {
        if (m_sel.empty())
            return;

        setdirty(m_sel.nb.row, m_sel.ne.row);
        m_sel.clear();
    }

    void selscroll(int orig, int n)
    {
        if (m_sel.empty())
            return;

        if ((orig <= m_sel.ob.row && m_sel.ob.row <= m_bot) ||
                (orig <= m_sel.oe.row && m_sel.oe.row <= m_bot)) {
            if ((m_sel.ob.row += n) > m_bot ||
                    (m_sel.oe.row += n) < m_top) {
                selclear();
                return;
            }
            if (m_sel.rectangular()) {
                if (m_sel.ob.row < m_top)
                    m_sel.ob.row = m_top;
                if (m_sel.oe.row > m_bot)
                    m_sel.oe.row = m_bot;
            } else {
                if (m_sel.ob.row < m_top) {
                    m_sel.ob.row = m_top;
                    m_sel.ob.col = 0;
                }
                if (m_sel.oe.row > m_bot) {
                    m_sel.oe.row = m_bot;
                    m_sel.oe.col = m_cols;
                }
            }
            selnormalize();
        }
    }

    void selnormalize()
    {
        if (!m_sel.rectangular() && m_sel.ob.row != m_sel.oe.row) {
            m_sel.nb.col = m_sel.ob.row < m_sel.oe.row ? m_sel.ob.col : m_sel.oe.col;
            m_sel.ne.col = m_sel.ob.row < m_sel.oe.row ? m_sel.oe.col : m_sel.ob.col;
        } else {
            m_sel.nb.col = std::min(m_sel.ob.col, m_sel.oe.col);
            m_sel.ne.col = std::max(m_sel.ob.col, m_sel.oe.col);
        }
        m_sel.nb.row = std::min(m_sel.ob.row, m_sel.oe.row);
        m_sel.ne.row = std::max(m_sel.ob.row, m_sel.oe.row);

        selsnap(&m_sel.nb.col, &m_sel.nb.row, -1);
        selsnap(&m_sel.ne.col, &m_sel.ne.row, +1);

        // expand selection over line breaks
        if (m_sel.rectangular())
            return;
        int i = linelen(m_sel.nb.row);
        if (i < m_sel.nb.col)
            m_sel.nb.col = i;
        if (linelen(m_sel.ne.row) <= m_sel.ne.col)
            m_sel.ne.col = m_cols - 1;
    }

    int linelen(int row)
    {
        int i = m_cols;

        if (attr({row, i - 1})[ATTR_WRAP])
            return i;

        while (i > 0 && glyph({row, i - 1}).u == empty_char)
            --i;

        return i;
    }

    bool isdirty(int row) const { return m_dirty[row]; }
    void setdirty() { setdirty(0, m_rows - 1); }
    void cleardirty(int row) { m_dirty[row] = false; }

    void setdirty(int top, int bot)
    {
        top = limit(top, 0, m_rows - 1);
        bot = limit(bot, 0, m_rows - 1);

        // todo: std::fill
        for (int i = top; i <= bot; i++)
            m_dirty[i] = true;

        m_bus->publish(event::Refresh{});
    }

    screenRows& lines() { return m_lines; }
    const screenRows& lines() const { return m_lines; }

    screenRow& line(int row) { return m_lines[row]; }
    const screenRow& line(int row) const { return m_lines[row]; }

    const int rows() const { return m_rows; }
    const int cols() const { return m_cols; }
    const int top() const { return m_top; }
    const int bot() const { return m_bot; }

    const Cursor& cursor() const { return m_cursor; }
    void setCursor(const Cursor& cursor)
    {
        m_cursor = cursor;
        m_bus->publish(event::Refresh{});
    }

    const Cursor& storedCursor(int idx) const
    {
        // todo: range checking
        return m_stored_cursors[idx];
    }

    void setStoredCursor(int idx, const Cursor& cursor)
    {
        // todo: range checking
        m_stored_cursors[idx] = cursor;
    }

    const cursor_type cursortype() const { return m_cursortype; }
    void setCursortype(cursor_type t) { m_cursortype = t; }

    Selection& sel() { return m_sel; };
    const Selection& sel() const { return m_sel; }

private:
    void fill(const Cell& begin, const Cell& end, const Glyph& val)
    {
        // note: assumes caller normalizes begin/end

        for (int row = begin.row; row <= end.row; row++) {
            auto lineit = m_lines[row].begin();
            std::fill(lineit + begin.col, lineit + end.col + 1, val);

            m_dirty[row] = true;
        }

        m_bus->publish(event::Refresh{});
    }

    std::shared_ptr<event::Bus> m_bus;
    screenRows m_lines;     // screen
    screenRows m_alt_lines; // alternate screen

    std::vector<bool> m_dirty; // dirtyness of lines

    int m_rows, m_cols; // size
    int m_top, m_bot;   // scroll limits

    Cursor m_cursor;
    Cursor m_stored_cursors[2];
    cursor_type m_cursortype;

    Selection m_sel;
};

Screen::Screen(std::shared_ptr<event::Bus> bus) :
    impl(std::make_unique<ScreenImpl>(std::move(bus)))
{}

Screen::~Screen() = default;

void Screen::reset()
{
    impl->reset();
}

void Screen::resize(int cols, int rows)
{
    impl->resize(cols, rows);
}

void Screen::swapscreen()
{
    impl->swapscreen();
}

void Screen::clear()
{
    impl->clear();
}

void Screen::clear(const Cell& begin, const Cell& end)
{
    impl->clear(begin, end);
}

const glyph_attribute& Screen::attr(const Cell& cell) const
{
    return impl->attr(cell);
}

Glyph& Screen::glyph(const Cell& cell)
{
    return impl->glyph(cell);
}

const Glyph& Screen::glyph(const Cell& cell) const
{
    return impl->glyph(cell);
}

void Screen::setGlyph(const Cell& cell, const Glyph& glyph)
{
    impl->setGlyph(cell, glyph);
}

void Screen::newline(bool first_col)
{
    impl->newline(first_col);
}

void Screen::deleteline(int n)
{
    impl->deleteline(n);
}

void Screen::insertblankline(int n)
{
    impl->insertblankline(n);
}

void Screen::deletechar(int n)
{
    impl->deletechar(n);
}

void Screen::insertblank(int n)
{
    impl->insertblank(n);
}

void Screen::setscroll(int t, int b)
{
    impl->setscroll(t, b);
}

void Screen::scrollup(int orig, int n)
{
    impl->scrollup(orig, n);
}

void Screen::scrolldown(int orig, int n)
{
    impl->scrolldown(orig, n);
}

void Screen::moveto(const Cell& cell)
{
    impl->moveto(cell);
}

void Screen::moveato(const Cell& cell)
{
    impl->moveato(cell);
}

void Screen::selsnap(int* col, int* row, int direction)
{
    impl->selsnap(col, row, direction);
}

void Screen::selclear()
{
    impl->selclear();
}

void Screen::selscroll(int orig, int n)
{
    impl->selscroll(orig, n);
}

void Screen::selnormalize()
{
    impl->selnormalize();
}

int Screen::linelen(int row)
{
    return impl->linelen(row);
}

bool Screen::isdirty(int row) const
{
    return impl->isdirty(row);
}

void Screen::setdirty()
{
    impl->setdirty();
}

void Screen::setdirty(int top, int bot)
{
    impl->setdirty(top, bot);
}

void Screen::cleardirty(int row)
{
    impl->cleardirty(row);
}

screenRows& Screen::lines()
{
    return impl->lines();
}

const screenRows& Screen::lines() const
{
    return impl->lines();
}

screenRow& Screen::line(int row)
{
    return impl->line(row);
}

const screenRow& Screen::line(int row) const
{
    return impl->line(row);
}

const int Screen::rows() const
{
    return impl->rows();
}

const int Screen::cols() const
{
    return impl->cols();
}

const int Screen::top() const
{
    return impl->top();
}

const int Screen::bot() const
{
    return impl->bot();
}

const Cursor& Screen::cursor() const
{
    return impl->cursor();
}

void Screen::setCursor(const Cursor& cursor)
{
    impl->setCursor(cursor);
}

const Cursor& Screen::storedCursor(int idx) const
{
    return impl->storedCursor(idx);
}

void Screen::setStoredCursor(int idx, const Cursor& cursor)
{
    impl->setStoredCursor(idx, cursor);
}

const cursor_type Screen::cursortype() const
{
    return impl->cursortype();
}

void Screen::setCursortype(cursor_type t)
{
    impl->setCursortype(t);
}

Selection& Screen::sel()
{
    return impl->sel();
}

const Selection& Screen::sel() const
{
    return impl->sel();
}

} // namespace screen
