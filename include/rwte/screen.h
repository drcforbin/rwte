#ifndef RWTE_SCREEN_H
#define RWTE_SCREEN_H

#include "rwte/coords.h"
#include "rwte/event.h"

#include <bitset>
#include <memory>
#include <vector>

class Selection;

namespace screen {

enum glyph_attribute_enum
{
    ATTR_BOLD,
    ATTR_FAINT,
    ATTR_ITALIC,
    ATTR_UNDERLINE,
    ATTR_BLINK,
    ATTR_REVERSE,
    ATTR_INVISIBLE,
    ATTR_STRUCK,
    ATTR_WRAP,
    ATTR_WIDE,
    ATTR_WDUMMY,
    ATTR_LAST = ATTR_WDUMMY
    // ATTR_BOLD_FAINT = ATTR_BOLD | ATTR_FAINT,
};

using glyph_attribute = std::bitset<ATTR_LAST + 1>;

const char32_t empty_char = ' ';

struct Glyph
{
    char32_t u = empty_char; // character code
    glyph_attribute attr;    // attribute flags
    uint32_t fg = 0;         // foreground
    uint32_t bg = 0;         // background
};

// todo: drop cursor_ prefix
enum class cursor_type
{
    CURSOR_BLINK_BLOCK,
    CURSOR_STEADY_BLOCK,
    CURSOR_BLINK_UNDER,
    CURSOR_STEADY_UNDER,
    CURSOR_BLINK_BAR,
    CURSOR_STEADY_BAR
};

// todo: make enum class, drop cursor_ prefix
enum cursor_state
{
    CURSOR_DEFAULT = 0,
    CURSOR_WRAPNEXT = 1,
    CURSOR_ORIGIN = 2
};

struct Cursor : public Cell
{
    Glyph attr; // current char attributes
    char state = 0;
};

using screenRow = std::vector<Glyph>;
using screenRows = std::vector<screenRow>;

class ScreenImpl;

class Screen
{
public:
    Screen(std::shared_ptr<event::Bus> bus);
    ~Screen();

    void reset();

    void resize(int cols, int rows);

    void swapscreen();

    void clear();
    // note: includes end
    void clear(const Cell& begin, const Cell& end);

    const glyph_attribute& attr(const Cell& cell) const;
    Glyph& glyph(const Cell& cell);
    const Glyph& glyph(const Cell& cell) const;
    void setGlyph(const Cell& cell, const Glyph& glyph);

    void newline(bool first_col);
    void deleteline(int n);
    void insertblankline(int n);
    void deletechar(int n);
    void insertblank(int n);

    void setscroll(int t, int b);
    void scrollup(int orig, int n);
    void scrolldown(int orig, int n);

    void moveto(const Cell& cell);
    // for absolute user moves, when decom is set
    void moveato(const Cell& cell);

    void selsnap(int* col, int* row, int direction);
    void selclear();
    void selscroll(int orig, int n);
    void selnormalize();
    std::shared_ptr<char> getsel() const;

    int linelen(int row) const;

    bool isdirty(int row) const;
    void setdirty();
    void setdirty(int top, int bot);
    void cleardirty(int row);

    screenRows& lines();
    const screenRows& lines() const;

    screenRow& line(int row);
    const screenRow& line(int row) const;

    const int rows() const;
    const int cols() const;
    const int top() const;
    const int bot() const;

    const Cursor& cursor() const;
    void setCursor(const Cursor& cursor);

    const Cursor& storedCursor(int idx) const;
    void setStoredCursor(int idx, const Cursor& cursor);

    const cursor_type cursortype() const;
    void setCursortype(cursor_type t);

    Selection& sel();
    const Selection& sel() const;

private:
    std::unique_ptr<ScreenImpl> impl;
};

} // namespace screen

#endif // RWTE_SCREEN_H
