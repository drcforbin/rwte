#ifndef RWTE_TERM_H
#define RWTE_TERM_H

#include "rwte/event.h"

#include <bitset>
#include <memory>

struct Cell;
class Selection;
namespace screen {
struct Cursor;
enum class cursor_type;
struct Glyph;
} // namespace screen
class Tty;
class Window;

namespace term {

enum mouse_event_enum
{
    MOUSE_MOTION = 0,
    MOUSE_PRESS = 1,
    MOUSE_RELEASE = 2
};

enum keymod_state_enum
{
    MOD_SHIFT,
    MOD_CTRL,
    MOD_ALT,
    MOD_LOGO,
    MOD_LAST = MOD_LOGO
};

using keymod_state = std::bitset<MOD_LAST + 1>;

enum term_mode_enum
{
    MODE_WRAP,
    MODE_INSERT,
    MODE_APPKEYPAD,
    MODE_ALTSCREEN,
    MODE_CRLF,
    MODE_MOUSEBTN,
    MODE_MOUSEMOTION,
    MODE_REVERSE,
    MODE_KBDLOCK,
    MODE_HIDE,
    MODE_ECHO,
    MODE_APPCURSOR,
    MODE_MOUSESGR,
    MODE_8BIT,
    MODE_BLINK,
    MODE_FBLINK,
    MODE_FOCUS,
    MODE_MOUSEX10,
    MODE_MOUSEMANY,
    MODE_BRCKTPASTE,
    MODE_PRINT,
    MODE_UTF8,
    MODE_SIXEL,
    MODE_LAST = MODE_SIXEL
};

using term_mode = std::bitset<MODE_LAST + 1>;

const term_mode mouse_modes(
        1 << MODE_MOUSEBTN | 1 << MODE_MOUSEMOTION |
        1 << MODE_MOUSEX10 | 1 << MODE_MOUSEMANY);

class TermImpl;

class Term
{
public:
    Term(std::shared_ptr<event::Bus> bus, int cols, int rows);
    ~Term();

    void setWindow(std::shared_ptr<Window> window);
    void setTty(std::shared_ptr<Tty> tty);

    const screen::Glyph& glyph(const Cell& cell) const;
    screen::Glyph& glyph(const Cell& cell);

    void reset();

    void setprint();

    const term_mode& mode() const;

    void blink();

    const Selection& sel() const;
    const screen::Cursor& cursor() const;
    screen::cursor_type cursortype() const;

    bool isdirty(int row) const;
    void setdirty();
    void cleardirty(int row);

    void putc(char32_t u);
    void mousereport(const Cell& cell, mouse_event_enum evt, int button,
            const keymod_state& mod);

    int rows() const;
    int cols() const;

    // default colors
    uint32_t deffg() const;
    uint32_t defbg() const;
    uint32_t defcs() const;
    uint32_t defrcs() const;

    void setfocused(bool focused);
    bool focused() const;

    void selclear();
    void clipcopy();

    void send(std::string_view data);

private:
    std::unique_ptr<TermImpl> impl;
};

} // namespace term

#endif // RWTE_TERM_H
