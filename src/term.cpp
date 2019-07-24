#include "fmt/format.h"
#include "lua/config.h"
#include "lua/state.h"
#include "lua/window.h"
#include "rwte/color.h"
#include "rwte/config.h"
#include "rwte/logging.h"
#include "rwte/rwte.h"
#include "rwte/screen.h"
#include "rwte/selection.h"
#include "rwte/term.h"
#include "rwte/tty.h"
#include "rwte/utf8.h"
#include "rwte/window.h"

#include <algorithm>
#include <cstdint>

#define LOGGER() (logging::get("term"))

namespace term {

template <typename T>
void defaultval(T& a, const T& b)
{
    a = a ? a : b;
}

time_t timediff(const timespec& t1, const timespec& t2)
{
    return (t1.tv_sec - t2.tv_sec) * 1000 +
           (t1.tv_nsec - t2.tv_nsec) / 1E6;
}

template <typename T,
        typename = typename std::enable_if<std::is_arithmetic<T>::value, T>::type>
constexpr T limit(T x, T a, T b)
{
    return x < a ? a : (x > b ? b : x);
}

static const keymod_state EMPTY_MASK; // no mods
static const keymod_state SHIFT_MASK(1 << MOD_SHIFT);
static const keymod_state ALT_MASK(1 << MOD_ALT);

enum charset
{
    CS_GRAPHIC0,
    CS_GRAPHIC1,
    CS_UK,
    CS_USA,
    CS_MULTI,
    CS_GER,
    CS_FIN
};

enum escape_state_enum
{
    ESC_START,
    ESC_CSI,
    ESC_STR, // OSC, PM, APC
    ESC_ALTCHARSET,
    ESC_STR_END, // a final string was encountered
    ESC_TEST,    // enter in test mode
    ESC_UTF8,
    ESC_DCS,
    ESC_LAST = ESC_DCS
};

using escape_state = std::bitset<ESC_LAST + 1>;

enum cursor_movement
{
    CURSOR_SAVE,
    CURSOR_LOAD
};

const int esc_buf_size = (128 * utf_size);
const int esc_arg_size = 16;
const int str_buf_size = esc_buf_size;
const int str_arg_size = esc_arg_size;

// CSI Escape sequence structs
// ESC '[' [[ [<priv>] <arg> [;]] <mode> [<mode>]]
struct CSIEscape
{
    char buf[esc_buf_size]; // raw string
    std::size_t len;        // raw string length
    bool priv;
    int arg[esc_arg_size];
    int narg; // num args
    char mode[2];
};

// STR Escape sequence structs
// ESC type [[ [<priv>] <arg> [;]] <mode>] ESC '\'
struct STREscape
{
    char type;              // ESC type ...
    char buf[str_buf_size]; // raw string
    int len;                // raw string length
    char* args[str_arg_size];
    int narg; // nb of args
};

// default values to use if we don't have
// a default value in config
static const int DEFAULT_TAB_SPACES = 8;
static const int DEFAULT_DCLICK_TIMEOUT = 300;
static const int DEFAULT_TCLICK_TIMEOUT = 600;

static bool allow_alt_screen()
{
    // check options first
    if (options.noalt)
        return false;

    // option not set, check lua config
    return lua::config::get_bool("allow_alt_screen", true);
}

static int32_t hexcolor(const char* src)
{
    int32_t idx = -1;
    unsigned long val;
    char* e;

    std::size_t in_len = std::strlen(src);
    if (in_len == 7 && src[0] == '#') {
        if ((val = strtoul(src + 1, &e, 16)) != ULONG_MAX && (e == src + 7))
            idx = 1 << 24 | val;
        else
            LOGGER()->error("erresc: invalid hex color ({})", src);
    } else
        LOGGER()->error("erresc: short hex color ({})", src);

    return idx;
}

static uint32_t defcolor(int* attr, int* npar, int l)
{
    int32_t idx = -1;
    uint r, g, b;

    switch (attr[*npar + 1]) {
        case 2: // direct color in RGB space
            if (*npar + 4 >= l) {
                LOGGER()->error("erresc(38): Incorrect number of parameters ({})", *npar);
                break;
            }
            r = attr[*npar + 2];
            g = attr[*npar + 3];
            b = attr[*npar + 4];
            *npar += 4;
            if (!(0 <= r && r <= 255) || !(0 <= g && g <= 255) || !(0 <= b && b <= 255))
                LOGGER()->error("erresc: bad rgb color ({},{},{})", r, g, b);
            else
                idx = color::truecol(r, g, b);
            break;
        case 5: // indexed color
            if (*npar + 2 >= l) {
                LOGGER()->error("erresc(38): Incorrect number of parameters ({})", *npar);
                break;
            }
            *npar += 2;
            if (!(0 <= attr[*npar] && attr[*npar] <= 255))
                LOGGER()->error("erresc: bad fgcolor {}", attr[*npar]);
            else
                idx = attr[*npar];
            break;
        case 0: /* implemented defined (only foreground) */
        case 1: /* transparent */
        case 3: /* direct color in CMY space */
        case 4: /* direct color in CMYK space */
        default:
            LOGGER()->error("erresc(38): gfx attr {} unknown", attr[*npar]);
            break;
    }

    return idx;
}

class TermImpl
{
public:
    TermImpl(std::shared_ptr<event::Bus> bus, int cols, int rows);
    ~TermImpl();

    void setWindow(std::shared_ptr<Window> window) { m_window = window; }
    void setTty(std::shared_ptr<Tty> tty) { m_tty = tty; }

    const screen::Glyph& glyph(const Cell& cell) const
    {
        return m_screen.glyph(cell);
    }

    screen::Glyph& glyph(const Cell& cell)
    {
        return m_screen.glyph(cell);
    }

    void reset();

    void setprint();

    const term_mode& mode() const { return m_mode; }

    void blink();

    const Selection& sel() const { return m_screen.sel(); }
    const screen::Cursor& cursor() const { return m_screen.cursor(); }
    screen::cursor_type cursortype() const { return m_screen.cursortype(); }

    bool isdirty(int row) const { return m_screen.isdirty(row); }
    void setdirty() { m_screen.setdirty(0, m_screen.rows() - 1); }
    void cleardirty(int row) { m_screen.cleardirty(row); }

    void putc(char32_t u);
    void mousereport(const Cell& cell, mouse_event_enum evt, int button,
            const keymod_state& mod);

    int rows() const { return m_screen.rows(); }
    int cols() const { return m_screen.cols(); }

    // default colors
    uint32_t deffg() const { return m_deffg; }
    uint32_t defbg() const { return m_defbg; }
    uint32_t defcs() const { return m_defcs; }
    uint32_t defrcs() const { return m_defrcs; }

    void setfocused(bool focused);
    bool focused() const { return m_focused; }

    void selclear() { m_screen.selclear(); }
    void clipcopy();

    void send(const char* data, std::size_t len);

private:
    void onresize(const event::Resize& evt);
    void resizeCore(int cols, int rows);
    void start_blink();

    void cursor(int mode);

    void swapscreen();

    void selsnap(int* col, int* row, int direction);

    void setchar(char32_t u, const screen::Glyph& attr, const Cell& cell);
    void defutf8(char ascii);
    void deftran(char ascii);
    void dectest(char c);
    void controlcode(unsigned char ascii);
    bool eschandle(unsigned char ascii);
    void resettitle();
    void puttab(int n);
    void strreset();
    void strparse();
    void strhandle();
    std::string strdump();
    void strsequence(unsigned char c);
    void csireset();
    void csiparse();
    void csihandle();
    std::string csidump();
    void setattr(int* attr, int len);
    void settmode(bool priv, bool set, int* args, int narg);
    void getbuttoninfo(const Cell& cell, const keymod_state& mod);
    std::shared_ptr<char> getsel();

    std::shared_ptr<event::Bus> m_bus;
    int m_resizeReg;

    screen::Screen m_screen;
    std::weak_ptr<Window> m_window;
    std::weak_ptr<Tty> m_tty;

    term_mode m_mode;         // terminal mode
    escape_state m_esc;       // escape mode
    std::vector<bool> m_tabs; // false where no tab, true where tab
    CSIEscape m_csiesc;
    STREscape m_stresc;

    int m_charset;  // current charset
    int m_icharset; // selected charset for sequence
    bool m_focused; // whether terminal has focus

    char m_trantbl[4];                            // charset table translation
    uint32_t m_deffg, m_defbg, m_defcs, m_defrcs; // default colors
};

TermImpl::TermImpl(std::shared_ptr<event::Bus> bus, int cols, int rows) :
    m_bus(std::move(bus)),
    m_resizeReg(m_bus->reg<event::Resize, TermImpl, &TermImpl::onresize>(this)),
    m_screen(m_bus),
    m_focused(false)
{
    strreset();
    csireset();

    // only a few things are initialized here
    // the rest happens in resize and reset
    // todo: pass cols, rows to m_screen ctor
    resizeCore(cols, rows);
    reset();
}

TermImpl::~TermImpl()
{
    m_bus->unreg<event::Resize>(m_resizeReg);
}

void TermImpl::reset()
{
    unsigned int i;
    int isnum = 0;

    // get config
    auto L = rwte->lua();
    L->getglobal("config");

    L->getfield(-1, "default_fg");
    uint32_t default_fg = L->tointegerx(-1, &isnum);
    if (!isnum)
        LOGGER()->fatal("config.default_fg is not an integer");

    L->getfield(-2, "default_bg");
    uint32_t default_bg = L->tointegerx(-1, &isnum);
    if (!isnum)
        LOGGER()->fatal("config.default_bg is not an integer");

    L->getfield(-3, "default_cs");
    uint32_t default_cs = L->tointegerx(-1, &isnum);
    if (!isnum)
        LOGGER()->fatal("config.default_cs is not an integer");

    L->getfield(-4, "default_rcs");
    uint32_t default_rcs = L->tointegerx(-1, &isnum);
    if (!isnum)
        LOGGER()->fatal("config.default_rcs is not an integer");

    L->getfield(-5, "tab_spaces");
    int tab_spaces = L->tointegerdef(-1, DEFAULT_TAB_SPACES);

    L->pop(6);

    screen::Cursor c{};
    c.attr.fg = default_fg;
    c.attr.bg = default_bg;

    m_screen.setCursor(c);
    m_screen.setStoredCursor(0, c);
    m_screen.setStoredCursor(1, c);

    m_deffg = default_fg;
    m_defbg = default_bg;
    m_defcs = default_cs;
    m_defrcs = default_rcs;

    for (i = 0; i < m_tabs.size(); i++) {
        if (i == 0 || i % tab_spaces != 0)
            m_tabs[i] = false;
        else
            m_tabs[i] = true;
    }

    // if set, print survives a reset,
    // and we always have wrap and utf8
    // enabled
    bool printing = m_mode[MODE_PRINT];
    m_mode.reset();
    m_mode.set(MODE_WRAP);
    m_mode.set(MODE_UTF8);
    if (printing)
        m_mode.set(MODE_PRINT);

    m_esc.reset();
    std::memset(m_trantbl, CS_USA, sizeof(m_trantbl));
    m_charset = 0;
    m_icharset = 0;

    for (i = 0; i < 2; i++) {
        m_screen.moveto({0, 0});
        cursor(CURSOR_SAVE);
    }

    m_screen.reset();

    // todo: needs blink func?
    if (m_screen.cursortype() == screen::cursor_type::CURSOR_BLINK_BLOCK ||
            m_screen.cursortype() == screen::cursor_type::CURSOR_BLINK_UNDER ||
            m_screen.cursortype() == screen::cursor_type::CURSOR_BLINK_BAR)
        start_blink();
}

void TermImpl::setprint()
{
    m_mode.set(MODE_PRINT);
}

void TermImpl::blink()
{
    bool need_blink =
            m_screen.cursortype() == screen::cursor_type::CURSOR_BLINK_BLOCK ||
            m_screen.cursortype() == screen::cursor_type::CURSOR_BLINK_UNDER ||
            m_screen.cursortype() == screen::cursor_type::CURSOR_BLINK_BAR;

    // see if we have anything blinking and mark blinking lines dirty
    for (int i = 0; i < m_screen.rows(); i++) {
        for (const auto& g : m_screen.line(i)) {
            if (g.attr[screen::ATTR_BLINK]) {
                need_blink = true;
                m_screen.setdirty(i, i);
                break;
            }
        }
    }

    if (need_blink) {
        // toggle blink
        m_mode[MODE_BLINK] = !m_mode[MODE_BLINK];
    } else {
        // reset and stop blinking
        m_mode[MODE_BLINK] = false;
        rwte->stop_blink();
    }

    rwte->refresh();
}

void TermImpl::onresize(const event::Resize& evt)
{
    resizeCore(evt.cols, evt.rows);
}

void TermImpl::resizeCore(int cols, int rows)
{
    LOGGER()->info("resize to {}x{}", cols, rows);

    int minrow = std::min(rows, m_screen.rows());
    int mincol = std::min(cols, m_screen.cols());

    if (cols < 1 || rows < 1) {
        LOGGER()->error("attempted resize to {}x{}", cols, rows);
        return;
    }

    // resize to new height
    m_tabs.resize(cols);

    if (cols > m_screen.cols()) {
        int tab_spaces = lua::config::get_int(
                "tab_spaces", DEFAULT_TAB_SPACES);

        // point to end of old size
        auto bp = m_tabs.cbegin() + m_screen.cols();
        // back up to last tab (or begin)
        if (bp != m_tabs.cbegin())
            while (--bp != m_tabs.cbegin() && !*bp) {
            }
        // set tabs from here (resize cleared newly added tabs)
        auto idx = static_cast<decltype(m_tabs)::size_type>(
                std::distance(m_tabs.cbegin(), bp));
        for (idx += tab_spaces; idx < m_tabs.size(); idx += tab_spaces)
            m_tabs[idx] = true;
    }

    // update terminal size
    m_screen.resize(cols, rows);

    // reset scrolling region
    m_screen.setscroll(0, rows - 1);
    // make use of the LIMIT in moveto
    m_screen.moveto(m_screen.cursor());

    // store cursor
    // todo: move to screen too, when mode moves. move cursor(int) too
    screen::Cursor c = m_screen.cursor();
    // clear both screens (dirties all lines)
    for (int i = 0; i < 2; i++) {
        if (mincol < cols && 0 < minrow)
            m_screen.clear({0, mincol}, {minrow - 1, cols - 1});
        if (0 < cols && minrow < rows)
            m_screen.clear({minrow, 0}, {rows - 1, cols - 1});
        swapscreen();
        cursor(CURSOR_LOAD);
    }
    // reset cursor
    m_screen.setCursor(c);
}

void TermImpl::start_blink()
{
    // reset mode every time this is called, so that the
    // cursor shows while the screen is being updated
    m_mode[MODE_BLINK] = false;
    rwte->start_blink();
}

void TermImpl::cursor(int mode)
{
    int alt = m_mode[MODE_ALTSCREEN] ? 1 : 0;

    if (mode == CURSOR_SAVE) {
        m_screen.setStoredCursor(alt, m_screen.cursor());
    } else if (mode == CURSOR_LOAD) {
        m_screen.setCursor(m_screen.storedCursor(alt));
        m_screen.moveto(m_screen.cursor());
    }
}

static bool iscontrolc0(char32_t c)
{
    return c <= 0x1f || c == '\177';
}

static bool iscontrolc1(char32_t c)
{
    return 0x80 <= c && c <= 0x9f;
}

static bool iscontrol(char32_t c)
{
    return iscontrolc0(c) || iscontrolc1(c);
}

void TermImpl::putc(char32_t u)
{
    char c[utf_size];
    int width;
    std::size_t len;

    bool control = iscontrol(u);

    // not UTF8 or SIXEL
    if (!m_mode[MODE_UTF8] && !m_mode[MODE_SIXEL]) {
        c[0] = u;
        width = len = 1;
    } else {
        len = utf8encode(u, c);
        if (!control && (width = wcwidth(u)) == -1) {
            std::memcpy(c, "\357\277\275", 4); /* UTF_INVALID */
            width = 1;
        }
    }

    if (m_mode[MODE_PRINT]) {
        if (auto tty = m_tty.lock())
            tty->print(c, len);
        else
            LOGGER()->debug("print without tty");
    }

    // STR sequence must be checked before anything else
    // because it uses all following characters until it
    // receives a ESC, a SUB, a ST or any other C1 control
    // character.
    if (m_esc[ESC_STR]) {
        if (u == '\a' || u == 030 || u == 032 || u == 033 ||
                iscontrolc1(u)) {
            m_esc.reset(ESC_START);
            m_esc.reset(ESC_STR);
            m_esc.reset(ESC_DCS);
            if (m_mode[MODE_SIXEL]) {
                // TODO: render sixel
                m_mode.reset(MODE_SIXEL);
                return;
            }
            m_esc.set(ESC_STR_END);
        } else {
            if (m_mode[MODE_SIXEL]) {
                // TODO: implement sixel mode
                return;
            }

            if (m_esc[ESC_DCS] && m_stresc.len == 0 && u == 'q')
                m_mode.set(MODE_SIXEL);

            if (m_stresc.len + len >= sizeof(m_stresc.buf) - 1) {
                // Here is a bug in terminals. If the user never sends
                // some code to stop the str or esc command, then we
                // will stop responding. But this is better than
                // silently failing with unknown characters. At least
                // then users will report back.
                //
                // In the case users ever get fixed, here is the code:
                //
                // m_esc.reset();
                // strhandle();
                //
                return;
            }

            std::memmove(&m_stresc.buf[m_stresc.len], c, len);
            m_stresc.len += len;
            return;
        }
    }

    // Actions of control codes must be performed as soon they arrive
    // because they can be embedded inside a control sequence, and
    // they must not cause conflicts with sequences.
    if (control) {
        controlcode(u);

        // control codes are not shown ever
        return;
    } else if (m_esc[ESC_START]) {
        if (m_esc[ESC_CSI]) {
            m_csiesc.buf[m_csiesc.len++] = u;
            if ((0x40 <= u && u <= 0x7E) ||
                    m_csiesc.len >= sizeof(m_csiesc.buf) - 1) {
                m_esc.reset();
                csiparse();
                csihandle();
            }
            return;
        } else if (m_esc[ESC_UTF8])
            defutf8(u);
        else if (m_esc[ESC_ALTCHARSET])
            deftran(u);
        else if (m_esc[ESC_TEST])
            dectest(u);
        else {
            if (!eschandle(u))
                return;

            // sequence already finished
        }

        m_esc.reset();

        // don't print sequence chars
        return;
    }

    auto& cursor = m_screen.cursor();
    const auto& sel = m_screen.sel();
    if (!sel.empty() &&
            sel.ob.row <= cursor.row && cursor.row <= sel.oe.row)
        m_screen.selclear();

    screen::Glyph* gp = &m_screen.glyph(cursor);
    if (m_mode[MODE_WRAP] && (cursor.state & screen::CURSOR_WRAPNEXT)) {
        gp->attr.set(screen::ATTR_WRAP);
        m_screen.newline(true);
        gp = &m_screen.glyph(cursor);
    }

    // todo: it's not cool to dig into / make assumptions about screen here
    if (m_mode[MODE_INSERT] && cursor.col + width < m_screen.cols())
        // todo: check
        std::memmove(gp + width, gp,
                (m_screen.cols() - cursor.col - width) * sizeof(screen::Glyph));

    if (cursor.col + width > m_screen.cols()) {
        m_screen.newline(true);
        gp = &m_screen.glyph(cursor);
    }

    setchar(u, cursor.attr, cursor);

    if (width == 2) {
        gp->attr.set(screen::ATTR_WIDE);
        if (cursor.col + 1 < m_screen.cols()) {
            gp[1].u = '\0';
            gp[1].attr.reset();
            gp[1].attr.set(screen::ATTR_WDUMMY);
        }
    }

    if (cursor.col + width < m_screen.cols())
        // todo: add move to rel to cursor
        m_screen.moveto({cursor.row, cursor.col + width});
    else {
        // todo: some better way than this temporary?
        screen::Cursor cur = cursor;
        cur.state |= screen::CURSOR_WRAPNEXT;
        m_screen.setCursor(cur);
    }
}

// bitfield for buttons
// low two indicate buttons:
//   00 means button 1 (or 4 with bit 7 set)
//   01 means button 2 (or 5 with bit 7 set)
//   10 means button 3
//   11 means release (not sent for buttons 4 or 5)
// bit 3 indicates shift
// bit 4 indicates meta/logo
// bit 5 indicates ctrl
// bit 6 indicates motion
// bit 7 means buttons are 4 or 5
enum mouseflags
{
    MOUSEFLAGS_BUTTON1 = 0,
    MOUSEFLAGS_BUTTON2 = 1,
    MOUSEFLAGS_BUTTON3 = 2,
    MOUSEFLAGS_RELEASE = 3,
    MOUSEFLAGS_SHIFT = 4,
    MOUSEFLAGS_LOGO = 8,
    MOUSEFLAGS_CTRL = 16,
    MOUSEFLAGS_MOTION = 32,
    MOUSEFLAGS_BUTTON4 = 64,
    MOUSEFLAGS_BUTTON5 = 65
};

// map of buttons to their bits
static const int button_map[5] = {
        MOUSEFLAGS_BUTTON1,
        MOUSEFLAGS_BUTTON2,
        MOUSEFLAGS_BUTTON3,
        MOUSEFLAGS_BUTTON4,
        MOUSEFLAGS_BUTTON5};

void TermImpl::mousereport(const Cell& cell, mouse_event_enum evt, int button,
        const keymod_state& mod)
{
    // todo: suspicious use of static
    static int oldbutton = MOUSEFLAGS_RELEASE;
    static Cell oldcell;

    if (evt == MOUSE_PRESS || evt == MOUSE_RELEASE) {
        if (button < 1 || 5 < button) {
            LOGGER()->error("button event {} for unexpected button {}", evt, button);
            return;
        }
    }

    if (LOGGER()->level() <= logging::trace) {
        std::string mode;
        if (m_mode[MODE_MOUSEBTN])
            mode = "BTN";
        if (m_mode[MODE_MOUSEMOTION]) {
            if (!mode.empty())
                mode += ",";
            mode += "MOT";
        }
        if (m_mode[MODE_MOUSEX10]) {
            if (!mode.empty())
                mode += ",";
            mode += "X10";
        }
        if (m_mode[MODE_MOUSEMANY]) {
            if (!mode.empty())
                mode += ",";
            mode += "MNY";
        }

        if (evt == MOUSE_MOTION) {
            LOGGER()->trace("mousereport MOTION {}, {}, oldbutton={}, mode={}",
                    cell.col, cell.row, oldbutton, mode);
        } else {
            LOGGER()->trace("mousereport {} {}, {}, {}, oldbutton={}, mode={}",
                    evt == MOUSE_PRESS ? "PRESS" : "RELEASE",
                    button, cell.col, cell.row, oldbutton, mode);
        }
    }

    // if FORCE_SEL_MOD is set and all modifiers are present
    bool forcesel = mod != EMPTY_MASK && (mod & FORCE_SEL_MOD) == FORCE_SEL_MOD;

    if ((m_mode & mouse_modes).any() && !forcesel) {
        // bitfield for buttons
        int cb;

        // from st, who got it from urxvt
        if (evt == MOUSE_MOTION) {
            // return if we haven't moved
            if (cell == oldcell)
                return;

            // motion is only reported in MOUSEMOTION and MOUSEMANY
            if (!m_mode[MODE_MOUSEMOTION] && !m_mode[MODE_MOUSEMANY])
                return;

            // MOUSEMOTION only reports when a button is pressed
            if (m_mode[MODE_MOUSEMOTION] && oldbutton == MOUSEFLAGS_RELEASE)
                return;

            cb = oldbutton | MOUSEFLAGS_MOTION;

            oldcell = cell;
        } else {
            if (!m_mode[MODE_MOUSESGR] && evt == MOUSE_RELEASE)
                cb = MOUSEFLAGS_RELEASE;
            else
                cb = button_map[button - 1]; // look up the button

            if (evt == MOUSE_PRESS) {
                oldbutton = cb;
                oldcell = cell;
            } else if (evt == MOUSE_RELEASE) {
                oldbutton = MOUSEFLAGS_RELEASE;

                // MODE_MOUSEX10: no button release reporting
                if (m_mode[MODE_MOUSEX10])
                    return;

                // release events are not reported for mousewheel buttons
                if (button == 4 || button == 5)
                    return;
            }
        }

        if (!m_mode[MODE_MOUSEX10]) {
            // except for X10 mode, when reporting
            // button we need to include shift, ctrl, meta/logo

            // probably never get shift
            cb |= (mod[MOD_SHIFT] ? MOUSEFLAGS_SHIFT : 0) |
                  (mod[MOD_LOGO] ? MOUSEFLAGS_LOGO : 0) |
                  (mod[MOD_CTRL] ? MOUSEFLAGS_CTRL : 0);
        }

        if (m_mode[MODE_MOUSESGR]) {
            if (auto tty = m_tty.lock()) {
                std::string seq = fmt::format("\033[<{};{};{}{:c}",
                        cb, cell.col + 1, cell.row + 1,
                        (evt == MOUSE_RELEASE) ? 'm' : 'M');
                tty->write(seq);
            } else
                LOGGER()->debug("tried to send SGR mouse without tty");
        } else if (cell.col < 223 && cell.row < 223) {
            if (auto tty = m_tty.lock()) {
                std::string seq = fmt::format("\033[M{:c}{:c}{:c}",
                        (char) (32 + cb), (char) (32 + cell.col + 1),
                        (char) (32 + cell.row + 1));
                tty->write(seq);
            } else
                LOGGER()->debug("tried to send extended mouse without tty");
        } else {
            // row or col is out of range...can't report unless
            // we're in MOUSESGR
            return;
        }
    } else {
        if (evt == MOUSE_PRESS) {
            auto L = rwte->lua();

            // todo: this call originates from term, when it really
            // makes a lot more sense to be called from window
            if (lua::window::call_mouse_press(L.get(), cell, button, mod))
                return;

            // start selection?
            if (button == 1) {
                L->getglobal("config");

                L->getfield(-1, "dclick_timeout");
                int dclick_timeout = L->tointegerdef(-1, DEFAULT_DCLICK_TIMEOUT);

                L->getfield(-2, "tclick_timeout");
                int tclick_timeout = L->tointegerdef(-1, DEFAULT_TCLICK_TIMEOUT);

                L->pop(3);

                timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);

                // clear previous selection, logically and visually.
                m_screen.selclear();

                // begin a selection
                auto& sel = m_screen.sel();
                sel.begin(cell);

                // if the user clicks below predefined timeouts specific
                // snapping behaviour is exposed.
                if (timediff(now, sel.tclick2) <= tclick_timeout)
                    sel.snap = Selection::Snap::Line;
                else if (timediff(now, sel.tclick1) <= dclick_timeout)
                    sel.snap = Selection::Snap::Word;
                else
                    sel.snap = Selection::Snap::None;

                m_screen.selnormalize();

                if (sel.snap != Selection::Snap::None)
                    sel.setmode(Selection::Mode::Ready);
                m_screen.setdirty(sel.nb.row, sel.ne.row);
                sel.tclick2 = sel.tclick1;
                sel.tclick1 = now;
            }
        } else if (evt == MOUSE_RELEASE) {
            if (button == 2) {
                if (auto window = m_window.lock())
                    window->selpaste();
                else
                    LOGGER()->debug("mouse release (2) without window");
            } else if (button == 1) {
                auto& sel = m_screen.sel();
                if (sel.mode() == Selection::Mode::Ready) {
                    getbuttoninfo(cell, mod);

                    // set primary sel and tell window about it
                    sel.primary = getsel();
                    if (auto window = m_window.lock())
                        window->setsel();
                    else
                        LOGGER()->debug("mouse release (1) without window");
                } else
                    m_screen.selclear();

                sel.setmode(Selection::Mode::Idle);
                m_screen.setdirty(sel.nb.row, sel.ne.row);
            }
        } else if (evt == MOUSE_MOTION) {
            auto& sel = m_screen.sel();
            if (sel.mode() == Selection::Mode::Idle)
                return;

            sel.setmode(Selection::Mode::Ready);
            Cell oldoe = sel.oe;
            int oldsbrow = sel.nb.row;
            int oldserow = sel.ne.row;
            getbuttoninfo(cell, mod);

            if (oldoe != sel.oe)
                m_screen.setdirty(std::min(sel.nb.row, oldsbrow),
                        std::max(sel.ne.row, oldserow));
        }
    }
}

void TermImpl::swapscreen()
{
    m_screen.swapscreen();

    m_mode.flip(MODE_ALTSCREEN);
}

void TermImpl::setfocused(bool focused)
{
    m_focused = focused;

    if (m_mode[MODE_FOCUS]) {
        if (auto tty = m_tty.lock()) {
            if (m_focused)
                tty->write("\033[I", 3);
            else
                tty->write("\033[O", 3);
        } else
            LOGGER()->debug("tried to send focus without tty");
    }

    rwte->refresh();
}

void TermImpl::clipcopy()
{
    // set clipboard sel and tell window about it
    m_screen.sel().clipboard = getsel();
    if (auto window = m_window.lock())
        window->setclip();
    else
        LOGGER()->debug("clip copy without window");
}

void TermImpl::send(const char* data, std::size_t len)
{
    if (auto tty = m_tty.lock())
        tty->write(data, len);
    else
        LOGGER()->debug("tried to send without tty");
}

void TermImpl::setchar(char32_t u, const screen::Glyph& attr, const Cell& cell)
{
    const char* const vt100_0[62] = {
            // 0x41 - 0x7e
            "↑", "↓", "→", "←", "█", "▚", "☃",                                      // A - G
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, // H - O
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, // P - W
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, " ",     // X - _
            "◆", "▒", "␉", "␌", "␍", "␊", "°", "±",                                 // ` - g
            "␤", "␋", "┘", "┐", "┌", "└", "┼", "⎺",                                 // h - o
            "⎻", "─", "⎼", "⎽", "├", "┤", "┴", "┬",                                 // p - w
            "│", "≤", "≥", "π", "≠", "£", "·",                                      // x - ~
    };

    // The table is proudly stolen from st, where it was
    // stolen from rxvt.
    if (m_trantbl[m_charset] == CS_GRAPHIC0 &&
            (0x41 <= u && u <= 0x7e) && vt100_0[u - 0x41])
        utf8decode(vt100_0[u - 0x41], &u, utf_size);

    auto thisGlyph = m_screen.glyph(cell);
    if (thisGlyph.attr[screen::ATTR_WIDE]) {
        if (cell.col + 1 < m_screen.cols()) {
            auto nextGlyph = m_screen.glyph({cell.row, cell.col + 1});
            nextGlyph.u = screen::empty_char;
            nextGlyph.attr.reset(screen::ATTR_WDUMMY);
            m_screen.setGlyph({cell.row, cell.col + 1}, nextGlyph);
        }
    } else if (thisGlyph.attr[screen::ATTR_WDUMMY]) {
        auto prevGlyph = m_screen.glyph({cell.row, cell.col - 1});
        prevGlyph.u = screen::empty_char;
        prevGlyph.attr.reset(screen::ATTR_WIDE);
        m_screen.setGlyph({cell.row, cell.col - 1}, prevGlyph);
    }

    thisGlyph = attr;
    thisGlyph.u = u;
    m_screen.setGlyph(cell, thisGlyph);

    if (attr.attr[screen::ATTR_BLINK])
        start_blink();
}

void TermImpl::defutf8(char ascii)
{
    if (ascii == 'G')
        m_mode.set(MODE_UTF8);
    else if (ascii == '@')
        m_mode.reset(MODE_UTF8);
}

void TermImpl::deftran(char ascii)
{
    const char* const cs = "0B";
    const int vcs[] = {CS_GRAPHIC0, CS_USA};

    const char* p;
    if ((p = std::strchr(cs, ascii)) == nullptr)
        LOGGER()->error("esc unhandled charset: ESC ( {}", ascii);
    else
        m_trantbl[m_icharset] = vcs[p - cs];
}

void TermImpl::dectest(char c)
{
    int col, row;

    if (c == '8') {
        // DEC screen alignment test
        auto& attr = m_screen.cursor().attr;
        for (col = 0; col < m_screen.cols(); ++col) {
            for (row = 0; row < m_screen.rows(); ++row)
                setchar('E', attr, {row, col});
        }
    }
}

void TermImpl::controlcode(unsigned char ascii)
{
    // LOGGER()->trace("ctrlcode {:02X}", (int) ascii);

    auto& cursor = m_screen.cursor();
    switch (ascii) {
        case '\t': // HT
            puttab(1);
            return;
        case '\b': // BS
            m_screen.moveto({cursor.row, cursor.col - 1});
            return;
        case '\r': // CR
            m_screen.moveto({cursor.row, 0});
            return;
        case '\f': // LF
        case '\v': // VT
        case '\n': // LF
            // go to first col if the mode is set
            m_screen.newline(m_mode[MODE_CRLF]);
            return;
        case '\a': // BEL
            if (m_esc[ESC_STR_END]) {
                // backwards compatibility to xterm
                strhandle();
            } else {
                if (!m_focused) {
                    if (auto window = m_window.lock())
                        window->seturgent(true);
                    else
                        LOGGER()->debug("set urgent without window");
                }

                // default bell_volume to 0 if invalid
                int bell_volume = lua::config::get_int("bell_volume", 0);
                bell_volume = limit(bell_volume, -100, 100);

                if (bell_volume) {
                    if (auto window = m_window.lock())
                        window->bell(bell_volume);
                    else
                        LOGGER()->debug("bell without window");
                }
            }
            break;
        case '\033': // ESC
            csireset();
            m_esc.reset(ESC_CSI);
            m_esc.reset(ESC_ALTCHARSET);
            m_esc.reset(ESC_TEST);
            m_esc.set(ESC_START);
            return;
        case '\016': // SO (LS1 -- Locking shift 1)
        case '\017': // SI (LS0 -- Locking shift 0)
            m_charset = 1 - (ascii - '\016');
            return;
        case '\032': // SUB
            setchar('?', cursor.attr, cursor);
        case '\030': // CAN
            csireset();
            break;
        case '\005': // ENQ (IGNORED)
        case '\000': // NUL (IGNORED)
        case '\021': // XON (IGNORED)
        case '\023': // XOFF (IGNORED)
        case 0177:   // DEL (IGNORED)
            return;
        case 0x80: // TODO: PAD
        case 0x81: // TODO: HOP
        case 0x82: // TODO: BPH
        case 0x83: // TODO: NBH
        case 0x84: // TODO: IND
            break;
        case 0x85:                  // NEL -- Next line
            m_screen.newline(true); // always go to first col
            break;
        case 0x86: // TODO: SSA
        case 0x87: // TODO: ESA
            break;
        case 0x88: // HTS -- Horizontal tab stop
            m_tabs[cursor.col] = true;
            break;
        case 0x89: // TODO: HTJ
        case 0x8a: // TODO: VTS
        case 0x8b: // TODO: PLD
        case 0x8c: // TODO: PLU
        case 0x8d: // TODO: RI
        case 0x8e: // TODO: SS2
        case 0x8f: // TODO: SS3
        case 0x91: // TODO: PU1
        case 0x92: // TODO: PU2
        case 0x93: // TODO: STS
        case 0x94: // TODO: CCH
        case 0x95: // TODO: MW
        case 0x96: // TODO: SPA
        case 0x97: // TODO: EPA
        case 0x98: // TODO: SOS
        case 0x99: // TODO: SGCI
            break;
        case 0x9a: // DECID -- Identify Terminal
        {
            // todo: refactor to function
            if (auto tty = m_tty.lock()) {
                auto term_id = lua::config::get_string("term_id");
                tty->write(term_id);
            } else
                LOGGER()->debug("tried to send termid (9a) without tty");
        } break;
        case 0x9b: // TODO: CSI
        case 0x9c: // TODO: ST
            break;
        case 0x90: // DCS -- Device Control String
        case 0x9d: // OSC -- Operating System Command
        case 0x9e: // PM -- Privacy Message
        case 0x9f: // APC -- Application Program Command
            strsequence(ascii);
            return;
    }
    // only CAN, SUB, \a and C1 chars interrupt a sequence
    m_esc.reset(ESC_STR_END);
    m_esc.reset(ESC_STR);
}

// returns true when the sequence is finished and it hasn't to read
// more characters for this sequence, otherwise false
bool TermImpl::eschandle(unsigned char ascii)
{
    auto& cursor = m_screen.cursor();
    switch (ascii) {
        case '[':
            m_esc.set(ESC_CSI);
            return false;
        case '#':
            m_esc.set(ESC_TEST);
            return false;
        case '%':
            m_esc.set(ESC_UTF8);
            return false;
        case 'P': // DCS -- Device Control String
        case '_': // APC -- Application Program Command
        case '^': // PM -- Privacy Message
        case ']': // OSC -- Operating System Command
        case 'k': // old title set compatibility
            strsequence(ascii);
            return false;
        case 'n': // LS2 -- Locking shift 2
        case 'o': // LS3 -- Locking shift 3
            m_charset = 2 + (ascii - 'n');
            break;
        case '(': // GZD4 -- set primary charset G0
        case ')': // G1D4 -- set secondary charset G1
        case '*': // G2D4 -- set tertiary charset G2
        case '+': // G3D4 -- set quaternary charset G3
            m_icharset = ascii - '(';
            m_esc.set(ESC_ALTCHARSET);
            return false;
        case 'D': // IND -- Linefeed
            if (cursor.row == m_screen.bot())
                m_screen.scrollup(m_screen.top(), 1);
            else
                m_screen.moveto({cursor.row + 1, cursor.col});
            break;
        case 'E':                   // NEL -- Next line
            m_screen.newline(true); // always go to first col
            break;
        case 'H': // HTS -- Horizontal tab stop
            m_tabs[cursor.col] = true;
            break;
        case 'M': // RI -- Reverse index
            if (cursor.row == m_screen.top())
                m_screen.scrolldown(m_screen.top(), 1);
            else
                m_screen.moveto({cursor.row - 1, cursor.col});
            break;
        case 'Z': // DECID -- Identify Terminal
        {
            // todo: refactor to function
            if (auto tty = m_tty.lock()) {
                auto term_id = lua::config::get_string("term_id");
                tty->write(term_id);
            } else
                LOGGER()->debug("tried to send termid (Z) without tty");
        } break;
        case 'c': // RIS -- Reset to inital state
            reset();
            resettitle();
            // todo: is this necessary?
            //xloadcols();
            break;
        case '=': // DECPAM -- Application keypad
            m_mode.set(MODE_APPKEYPAD);
            break;
        case '>': // DECPNM -- Normal keypad
            m_mode.reset(MODE_APPKEYPAD);
            break;
        case '7': // DECSC -- Save Cursor
            TermImpl::cursor(CURSOR_SAVE);
            break;
        case '8': // DECRC -- Restore Cursor
            TermImpl::cursor(CURSOR_LOAD);
            break;
        case '\\': // ST -- String Terminator
            if (m_esc[ESC_STR_END])
                strhandle();
            break;
        default:
            LOGGER()->error("unknown sequence ESC 0x{:02X} '{}'",
                    (unsigned char) ascii, isprint(ascii) ? ascii : '.');
            break;
    }
    return true;
}

void TermImpl::resettitle()
{
    if (auto window = m_window.lock())
        window->settitle(options.title);
    else
        LOGGER()->debug("reset title without window");
}

void TermImpl::puttab(int n)
{
    auto cursor = m_screen.cursor();
    int col = cursor.col;

    if (n > 0) {
        while (col < m_screen.cols() && n--)
            for (++col; col < m_screen.cols() && !m_tabs[col]; ++col)
                ; // nothing
    } else if (n < 0) {
        while (col > 0 && n++)
            for (--col; col > 0 && !m_tabs[col]; --col)
                ; // nothing
    }

    cursor.col = limit(col, 0, m_screen.cols() - 1);
    m_screen.setCursor(cursor);
}

void TermImpl::strreset()
{
    std::memset(&m_stresc, 0, sizeof(m_stresc));
}

void TermImpl::strparse()
{
    int c;
    char* p = m_stresc.buf;

    m_stresc.narg = 0;
    m_stresc.buf[m_stresc.len] = '\0';

    if (*p == '\0')
        return;

    while (m_stresc.narg < str_arg_size) {
        m_stresc.args[m_stresc.narg++] = p;
        while ((c = *p) != ';' && c != '\0')
            ++p;
        if (c == '\0')
            return;
        *p++ = '\0';
    }
}

void TermImpl::strhandle()
{
    // todo: color
    // char *p = nullptr;
    // int j;

    int narg, par;

    m_esc.reset(ESC_STR_END);
    m_esc.reset(ESC_STR);
    strparse();
    par = (narg = m_stresc.narg) ? atoi(m_stresc.args[0]) : 0;

    LOGGER()->trace("strhandle {}", strdump());

    switch (m_stresc.type) {
        case ']': // OSC -- Operating System Command
            switch (par) {
                case 0:
                case 1:
                case 2:
                    if (narg > 1) {
                        if (auto window = m_window.lock())
                            window->settitle(m_stresc.args[1]);
                        else
                            LOGGER()->debug("set title (OSC 0,1,2) without window");
                    }
                    return;
                case 11:
                    if (narg > 1) {
                        int32_t color;
                        if ((color = hexcolor(m_stresc.args[1])) >= 0) {
                            m_defbg = static_cast<uint32_t>(color);
                            // todo...doesn't fully repaint?!?
                            // oh, I bet we need to reset the color on
                            // glyphs that are already set to something
                            setdirty();
                        }
                    }
                    return;
                case 52:
                    // todo: remove dump
                    LOGGER()->debug("OSC 52: {}", strdump());
                    if (narg > 2) {
                        // todo: color
                        //char *dec = base64dec(m_stresc.args[2]);
                        //if (dec) {
                        //    xsetsel(dec, CurrentTime);
                        //    clipcopy(NULL);
                        //} else {
                        //    fprintf(stderr, "erresc: invalid base64\n");
                        //}
                    }
                    return;
                case 4: // color set
                    if (narg < 3)
                        break;
                    // todo: color
                    // p = m_stresc.args[2];
                    // FALLTHROUGH
                case 104: // color reset, here p = NULL
                    // todo: remove dump
                    LOGGER()->debug("OSC 4/104: {}", strdump());
                    /*
            j = (narg > 1) ? atoi(m_stresc.args[1]) : -1;
            todo: color
            if (xsetcolorname(j, p)) {
                if (p == 104 && narg <= 1)
                    return; // color reset without parameter
                fprintf(stderr, "erresc: invalid color j=%d, p=%s\n",
                    j, p? p : "(null)");
            } else {
                // todo...doesn't fully repaint?!?
                // oh, I bet we need to reset the color on
                // glyphs that are already set to something
                setdirty();
            }
            */
                    return;
            }
            break;
        case 'k': // old title set compatibility
            if (auto window = m_window.lock())
                window->settitle(m_stresc.args[0]);
            else
                LOGGER()->debug("set title (k) without window");
            return;
        case 'P': // DCS -- Device Control String
            m_esc.set(ESC_DCS);
        case '_': // APC -- Application Program Command
        case '^': // PM -- Privacy Message
            return;
    }

    LOGGER()->error("unknown stresc: {}", strdump());
}

std::string TermImpl::strdump()
{
    fmt::memory_buffer msg;
    fmt::writer writer(msg);

    writer.write("ESC");
    writer.write(m_stresc.type);

    for (int i = 0; i < m_stresc.len; i++) {
        unsigned int c = m_stresc.buf[i] & 0xff;
        if (c == '\0')
            return fmt::to_string(msg); // early exit
        else if (isprint(c))
            writer.write(static_cast<char>(c));
        else if (c == '\n')
            writer.write("(\\n)");
        else if (c == '\r')
            writer.write("(\\r)");
        else if (c == 0x1b)
            writer.write("(\\e)");
        else
            fmt::format_to(msg, "(0x{:02X})", c);
    }

    writer.write("ESC\\\n");
    return fmt::to_string(msg);
}

void TermImpl::strsequence(unsigned char c)
{
    strreset();

    switch (c) {
        case 0x90: // DCS -- Device Control String
            c = 'P';
            m_esc.set(ESC_DCS);
            break;
        case 0x9f: // APC -- Application Program Command
            c = '_';
            break;
        case 0x9e: // PM -- Privacy Message
            c = '^';
            break;
        case 0x9d: // OSC -- Operating System Command
            c = ']';
            break;
    }

    m_stresc.type = c;
    m_esc.set(ESC_STR);
}

void TermImpl::csireset()
{
    std::memset(&m_csiesc, 0, sizeof(m_csiesc));
}

void TermImpl::csiparse()
{
    char *p = m_csiesc.buf, *np;
    long int v;

    m_csiesc.narg = 0;
    if (*p == '?') {
        m_csiesc.priv = true;
        p++;
    }

    m_csiesc.buf[m_csiesc.len] = '\0';
    while (p < m_csiesc.buf + m_csiesc.len) {
        np = nullptr;
        v = strtol(p, &np, 10);
        if (np == p)
            v = 0;
        if (v == LONG_MAX || v == LONG_MIN)
            v = -1;
        m_csiesc.arg[m_csiesc.narg++] = v;
        p = np;
        if (*p != ';' || m_csiesc.narg == esc_arg_size)
            break;
        p++;
    }
    m_csiesc.mode[0] = *p++;
    m_csiesc.mode[1] = (p < m_csiesc.buf + m_csiesc.len) ? *p : '\0';
}

void TermImpl::csihandle()
{
    LOGGER()->trace("csiesc {}", csidump());

    auto& cursor = m_screen.cursor();
    switch (m_csiesc.mode[0]) {
        case '@': // ICH -- Insert <n> blank char
            defaultval(m_csiesc.arg[0], 1);
            m_screen.insertblank(m_csiesc.arg[0]);
            break;
        case 'A': // CUU -- Cursor <n> Up
            defaultval(m_csiesc.arg[0], 1);
            m_screen.moveto({cursor.row - m_csiesc.arg[0], cursor.col});
            break;
        case 'B': // CUD -- Cursor <n> Down
        case 'e': // VPR --Cursor <n> Down
            defaultval(m_csiesc.arg[0], 1);
            m_screen.moveto({cursor.row + m_csiesc.arg[0], cursor.col});
            break;
        case 'i': // MC -- Media Copy
            switch (m_csiesc.arg[0]) {
                /*
        todo: case media copy
        case 0:
            tdump();
            break;
        case 1:
            tdumpline(cursor.row);
            break;
        case 2:
            tdumpsel();
            break;
            */
                case 4:
                    m_mode.reset(MODE_PRINT);
                    break;
                case 5:
                    m_mode.set(MODE_PRINT);
                    break;
            }
            break;
        case 'c': // DA -- Device Attributes
            if (m_csiesc.arg[0] == 0) {
                // todo: refactor to function
                if (auto tty = m_tty.lock()) {
                    auto term_id = lua::config::get_string("term_id");
                    tty->write(term_id);
                } else
                    LOGGER()->debug("tried to send termid (c) without tty");
            }
            break;
        case 'C': // CUF -- Cursor <n> Forward
        case 'a': // HPR -- Cursor <n> Forward
            defaultval(m_csiesc.arg[0], 1);
            m_screen.moveto({cursor.row, cursor.col + m_csiesc.arg[0]});
            break;
        case 'D': // CUB -- Cursor <n> Backward
            defaultval(m_csiesc.arg[0], 1);
            m_screen.moveto({cursor.row, cursor.col - m_csiesc.arg[0]});
            break;
        case 'E': // CNL -- Cursor <n> Down and first col
            defaultval(m_csiesc.arg[0], 1);
            m_screen.moveto({cursor.row + m_csiesc.arg[0], 0});
            break;
        case 'F': // CPL -- Cursor <n> Up and first col
            defaultval(m_csiesc.arg[0], 1);
            m_screen.moveto({cursor.row - m_csiesc.arg[0], 0});
            break;
        case 'g': // TBC -- Tabulation clear
            switch (m_csiesc.arg[0]) {
                case 0: // clear current tab stop
                    m_tabs[cursor.col] = false;
                    break;
                case 3: // clear all the tabs
                    for (auto it = m_tabs.begin(); it != m_tabs.end(); it++)
                        *it = false;
                    break;
                default:
                    goto unknown;
            }
            break;
        case 'G': // CHA -- Move to <col>
        case '`': // HPA
            defaultval(m_csiesc.arg[0], 1);
            m_screen.moveto({cursor.row, m_csiesc.arg[0] - 1});
            break;
        case 'H': // CUP -- Move to <row> <col>
        case 'f': // HVP
            defaultval(m_csiesc.arg[0], 1);
            defaultval(m_csiesc.arg[1], 1);
            m_screen.moveato({m_csiesc.arg[0] - 1, m_csiesc.arg[1] - 1});
            break;
        case 'I': // CHT -- Cursor Forward Tabulation <n> tab stops
            defaultval(m_csiesc.arg[0], 1);
            puttab(m_csiesc.arg[0]);
            break;
        case 'J': // ED -- Clear screen
            m_screen.selclear();
            switch (m_csiesc.arg[0]) {
                case 0: // below
                    m_screen.clear(cursor, {cursor.row, m_screen.cols() - 1});
                    if (cursor.row < m_screen.rows() - 1) {
                        m_screen.clear({cursor.row + 1, 0},
                                {m_screen.rows() - 1, m_screen.cols() - 1});
                    }
                    break;
                case 1: // above
                    if (cursor.row > 1)
                        m_screen.clear({0, 0}, {cursor.row - 1, m_screen.cols() - 1});
                    m_screen.clear({cursor.row, 0}, cursor);
                    break;
                case 2: // all
                    m_screen.clear();
                    break;
                default:
                    goto unknown;
            }
            break;
        case 'K': // EL -- Clear line
            switch (m_csiesc.arg[0]) {
                case 0: // right
                    m_screen.clear(cursor, {cursor.row, m_screen.cols() - 1});
                    break;
                case 1: // left
                    m_screen.clear({cursor.row, 0}, cursor);
                    break;
                case 2: // all
                    m_screen.clear({cursor.row, 0}, {cursor.row, m_screen.cols() - 1});
                    break;
            }
            break;
        case 'S': // SU -- Scroll <n> line up
            defaultval(m_csiesc.arg[0], 1);
            m_screen.scrollup(m_screen.top(), m_csiesc.arg[0]);
            break;
        case 'T': // SD -- Scroll <n> line down
            defaultval(m_csiesc.arg[0], 1);
            m_screen.scrolldown(m_screen.top(), m_csiesc.arg[0]);
            break;
        case 'L': // IL -- Insert <n> blank lines
            defaultval(m_csiesc.arg[0], 1);
            m_screen.insertblankline(m_csiesc.arg[0]);
            break;
        case 'l': // RM -- Reset Mode
            settmode(m_csiesc.priv, false, m_csiesc.arg, m_csiesc.narg);
            break;
        case 'M': // DL -- Delete <n> lines
            defaultval(m_csiesc.arg[0], 1);
            m_screen.deleteline(m_csiesc.arg[0]);
            break;
        case 'X': // ECH -- Erase <n> char
            defaultval(m_csiesc.arg[0], 1);
            m_screen.clear(cursor, {cursor.row, cursor.col + m_csiesc.arg[0] - 1});
            break;
        case 'P': // DCH -- Delete <n> char
            defaultval(m_csiesc.arg[0], 1);
            m_screen.deletechar(m_csiesc.arg[0]);
            break;
        case 'Z': // CBT -- Cursor Backward Tabulation <n> tab stops
            defaultval(m_csiesc.arg[0], 1);
            puttab(-m_csiesc.arg[0]);
            break;
        case 'd': // VPA -- Move to <row>
            defaultval(m_csiesc.arg[0], 1);
            m_screen.moveato({m_csiesc.arg[0] - 1, cursor.col});
            break;
        case 'h': // SM -- Set terminal mode
            settmode(m_csiesc.priv, true, m_csiesc.arg, m_csiesc.narg);
            break;
        case 'm': // SGR -- Terminal attribute (color)
            setattr(m_csiesc.arg, m_csiesc.narg);
            break;
        case 'n': // DSR – Device Status Report (cursor position)
            if (m_csiesc.arg[0] == 6) {
                if (auto tty = m_tty.lock()) {
                    std::string seq = fmt::format(
                            "\033[{};{}R",
                            cursor.row + 1, cursor.col + 1);
                    tty->write(seq);
                } else
                    LOGGER()->debug("report cursor status without tty");
            }
            break;
        case 'r': // DECSTBM -- Set Scrolling Region
            if (m_csiesc.priv)
                goto unknown;
            else {
                defaultval(m_csiesc.arg[0], 1);
                defaultval(m_csiesc.arg[1], m_screen.rows());
                m_screen.setscroll(m_csiesc.arg[0] - 1, m_csiesc.arg[1] - 1);
                m_screen.moveato({0, 0});
            }
            break;
        case 's': // DECSC -- Save cursor position (ANSI.SYS)
            TermImpl::cursor(CURSOR_SAVE);
            break;
        case 'u': // DECRC -- Restore cursor position (ANSI.SYS)
            TermImpl::cursor(CURSOR_LOAD);
            break;
        case ' ':
            switch (m_csiesc.mode[1]) {
                case 'q': // DECSCUSR -- Set Cursor Style
                    defaultval(m_csiesc.arg[0], 1);
                    switch (m_csiesc.arg[0]) {
                        case 2: // Steady Block
                            m_screen.setCursortype(screen::cursor_type::CURSOR_STEADY_BLOCK);
                            break;
                        case 3: // Blinking Underline
                            m_screen.setCursortype(screen::cursor_type::CURSOR_BLINK_UNDER);
                            start_blink();
                            break;
                        case 4: // Steady Underline
                            m_screen.setCursortype(screen::cursor_type::CURSOR_STEADY_UNDER);
                            break;
                        case 5: // Blinking bar
                            m_screen.setCursortype(screen::cursor_type::CURSOR_BLINK_BAR);
                            start_blink();
                            break;
                        case 6: // Steady bar
                            m_screen.setCursortype(screen::cursor_type::CURSOR_STEADY_BAR);
                            break;
                        case 0: // Blinking Block
                        case 1: // Blinking Block (Default)
                        default:
                            m_screen.setCursortype(screen::cursor_type::CURSOR_BLINK_BLOCK);
                            start_blink();
                            LOGGER()->error("unknown cursor {}", m_csiesc.arg[0]);
                            break;
                    }
                    break;
                default:
                    goto unknown;
            }
            break;
        default:
        unknown:
            LOGGER()->error("unknown csiesc {}: {}",
                    m_csiesc.mode[0], csidump());
            break;
    }
}

std::string TermImpl::csidump()
{
    fmt::memory_buffer msg;
    fmt::writer writer(msg);

    writer.write("ESC[");

    for (std::size_t i = 0; i < m_csiesc.len; i++) {
        unsigned int c = m_csiesc.buf[i] & 0xff;
        if (isprint(c))
            writer.write(static_cast<char>(c));
        else if (c == '\n')
            writer.write("(\\n)");
        else if (c == '\r')
            writer.write("(\\r)");
        else if (c == 0x1b)
            writer.write("(\\e)");
        else
            fmt::format_to(msg, "(0x{:02X})", c);
    }

    return fmt::to_string(msg);
}

void TermImpl::setattr(int* attr, int len)
{
    // todo: need track more than cursor attr,
    // how can this be implemented
    auto cursor = m_screen.cursor();
    for (int i = 0; i < len; i++) {
        switch (attr[i]) {
            case 0:
                cursor.attr.attr.reset(screen::ATTR_BOLD);
                cursor.attr.attr.reset(screen::ATTR_FAINT);
                cursor.attr.attr.reset(screen::ATTR_ITALIC);
                cursor.attr.attr.reset(screen::ATTR_UNDERLINE);
                cursor.attr.attr.reset(screen::ATTR_BLINK);
                cursor.attr.attr.reset(screen::ATTR_REVERSE);
                cursor.attr.attr.reset(screen::ATTR_INVISIBLE);
                cursor.attr.attr.reset(screen::ATTR_STRUCK);
                cursor.attr.fg = m_deffg;
                cursor.attr.bg = m_defbg;
                m_screen.setCursor(cursor);
                break;
            case 1:
                cursor.attr.attr.set(screen::ATTR_BOLD);
                m_screen.setCursor(cursor);
                break;
            case 2:
                cursor.attr.attr.set(screen::ATTR_FAINT);
                m_screen.setCursor(cursor);
                break;
            case 3:
                cursor.attr.attr.set(screen::ATTR_ITALIC);
                m_screen.setCursor(cursor);
                break;
            case 4:
                cursor.attr.attr.set(screen::ATTR_UNDERLINE);
                m_screen.setCursor(cursor);
                break;
            case 5: // slow blink
            case 6: // rapid blink
                cursor.attr.attr.set(screen::ATTR_BLINK);
                m_screen.setCursor(cursor);
                break;
            case 7:
                cursor.attr.attr.set(screen::ATTR_REVERSE);
                m_screen.setCursor(cursor);
                break;
            case 8:
                cursor.attr.attr.set(screen::ATTR_INVISIBLE);
                m_screen.setCursor(cursor);
                break;
            case 9:
                cursor.attr.attr.set(screen::ATTR_STRUCK);
                m_screen.setCursor(cursor);
                break;
            case 22:
                cursor.attr.attr.reset(screen::ATTR_BOLD);
                cursor.attr.attr.reset(screen::ATTR_FAINT);
                m_screen.setCursor(cursor);
                break;
            case 23:
                cursor.attr.attr.reset(screen::ATTR_ITALIC);
                m_screen.setCursor(cursor);
                break;
            case 24:
                cursor.attr.attr.reset(screen::ATTR_UNDERLINE);
                m_screen.setCursor(cursor);
                break;
            case 25:
                cursor.attr.attr.reset(screen::ATTR_BLINK);
                m_screen.setCursor(cursor);
                break;
            case 27:
                cursor.attr.attr.reset(screen::ATTR_REVERSE);
                m_screen.setCursor(cursor);
                break;
            case 28:
                cursor.attr.attr.reset(screen::ATTR_INVISIBLE);
                m_screen.setCursor(cursor);
                break;
            case 29:
                cursor.attr.attr.reset(screen::ATTR_STRUCK);
                m_screen.setCursor(cursor);
                break;
            case 38: {
                auto color = defcolor(attr, &i, len);
                if (color >= 0) {
                    cursor.attr.fg = color;
                    m_screen.setCursor(cursor);
                }
                break;
            }
            case 39:
                cursor.attr.fg = m_deffg;
                m_screen.setCursor(cursor);
                break;
            case 48: {
                auto color = defcolor(attr, &i, len);
                if (color >= 0) {
                    cursor.attr.bg = color;
                    m_screen.setCursor(cursor);
                }
                break;
            }
            case 49:
                cursor.attr.bg = m_defbg;
                m_screen.setCursor(cursor);
                break;
            default:
                if (30 <= attr[i] && attr[i] <= 37) {
                    cursor.attr.fg = attr[i] - 30;
                    m_screen.setCursor(cursor);
                } else if (40 <= attr[i] && attr[i] <= 47) {
                    cursor.attr.bg = attr[i] - 40;
                    m_screen.setCursor(cursor);
                } else if (90 <= attr[i] && attr[i] <= 97) {
                    cursor.attr.fg = attr[i] - 90 + 8;
                    m_screen.setCursor(cursor);
                } else if (100 <= attr[i] && attr[i] <= 107) {
                    cursor.attr.bg = attr[i] - 100 + 8;
                    m_screen.setCursor(cursor);
                } else {
                    LOGGER()->error(
                            "erresc(default): gfx attr {} unknown, {}",
                            attr[i], csidump());
                }
                break;
        }
    }
}

void TermImpl::settmode(bool priv, bool set, int* args, int narg)
{
    int* lim;
    term_mode mode;
    int alt;

    for (lim = args + narg; args < lim; ++args) {
        if (priv) {
            switch (*args) {
                case 1: // DECCKM -- Cursor key
                    m_mode.set(MODE_APPCURSOR, set);
                    break;
                case 5: // DECSCNM -- Reverse video
                    mode = m_mode;
                    m_mode.set(MODE_REVERSE, set);
                    if (mode != m_mode)
                        rwte->refresh();
                    break;
                case 6: // DECOM -- Origin
                {
                    auto cursor = m_screen.cursor();
                    if (set)
                        cursor.state |= screen::CURSOR_ORIGIN;
                    else
                        cursor.state &= ~screen::CURSOR_ORIGIN;
                    m_screen.setCursor(cursor);

                    m_screen.moveato({0, 0});
                    break;
                }
                case 7: // DECAWM -- Auto wrap
                    m_mode.set(MODE_WRAP, set);
                    break;
                case 0:  // Error (IGNORED)
                case 2:  // DECANM -- ANSI/VT52 (IGNORED)
                case 3:  // DECCOLM -- Column  (IGNORED)
                case 4:  // DECSCLM -- Scroll (IGNORED)
                case 8:  // DECARM -- Auto repeat (IGNORED)
                case 18: // DECPFF -- Printer feed (IGNORED)
                case 19: // DECPEX -- Printer extent (IGNORED)
                case 42: // DECNRCM -- National characters (IGNORED)
                case 12: // att610 -- Start blinking cursor (IGNORED)
                    break;
                case 25: // DECTCEM -- Text Cursor Enable Mode
                    m_mode.set(MODE_HIDE, !set);
                    break;
                case 9: // X10 mouse compatibility mode
                    m_mode &= ~mouse_modes;
                    m_mode.set(MODE_MOUSEX10, set);
                    break;
                case 1000: // 1000: VT200 mouse, report button press and release
                    m_mode &= ~mouse_modes;
                    m_mode.set(MODE_MOUSEBTN, set);
                    break;
                case 1002: // 1002: report motion on button press
                    m_mode &= ~mouse_modes;
                    m_mode.set(MODE_MOUSEMOTION, set);
                    break;
                case 1003: // 1003: enable all mouse motions
                    m_mode &= ~mouse_modes;
                    m_mode.set(MODE_MOUSEMANY, set);
                    break;
                case 1004: // 1004: send focus events to tty
                    m_mode.set(MODE_FOCUS, set);
                    break;
                case 1006: // 1006: extended reporting mode
                    m_mode.set(MODE_MOUSESGR, set);
                    break;
                case 1034:
                    m_mode.set(MODE_8BIT, set);
                    break;
                case 1049: // swap screen & set/restore cursor as xterm
                    if (!allow_alt_screen())
                        break;
                    TermImpl::cursor(set ? CURSOR_SAVE : CURSOR_LOAD);
                    // FALLTHROUGH
                case 47: // swap screen
                case 1047:
                    if (!allow_alt_screen())
                        break;
                    alt = m_mode[MODE_ALTSCREEN];
                    if (alt)
                        m_screen.clear();
                    if (set ^ alt)
                        swapscreen();
                    if (*args != 1049)
                        break;
                    // FALLTHROUGH
                case 1048:
                    TermImpl::cursor(set ? CURSOR_SAVE : CURSOR_LOAD);
                    break;
                case 2004: // 2004: bracketed paste mode
                    m_mode.set(MODE_BRCKTPASTE, set);
                    break;
                // unimplemented mouse modes:
                case 1001: // VT200 mouse highlight mode; can hang the terminal
                case 1005: // UTF-8 mouse mode; will confuse non-UTF-8 applications
                case 1015: // urxvt mangled mouse mode; incompatible
                           // and can be mistaken for other control codes
                    LOGGER()->warn("unsupported mouse mode requested {}", *args);
                    break;
                default:
                    LOGGER()->error(
                            "erresc: unknown private set/reset mode {}",
                            *args);
                    break;
            }
        } else {
            switch (*args) {
                case 0: // Error (IGNORED)
                    break;
                case 2: // KAM -- keyboard action
                    m_mode.set(MODE_KBDLOCK, set);
                    break;
                case 4: // IRM -- Insertion-replacement
                    m_mode.set(MODE_INSERT, set);
                    break;
                case 12: // SRM -- Send/Receive
                    m_mode.set(MODE_ECHO, !set);
                    break;
                case 20: // LNM -- Linefeed/new line
                    m_mode.set(MODE_CRLF, set);
                    break;
                default:
                    LOGGER()->error(
                            "erresc: unknown set/reset mode {}",
                            *args);
                    break;
            }
        }
    }
}

void TermImpl::getbuttoninfo(const Cell& cell, const keymod_state& mod)
{
    auto& sel = m_screen.sel();
    sel.alt = m_mode[MODE_ALTSCREEN];

    sel.oe = cell;
    m_screen.selnormalize();

    // TODO: move to lua code?
    // consider leaving it in the rectangular state if it
    // was started with alt, but alt was released
    sel.setrectangular((mod & ALT_MASK) == ALT_MASK);
}

// todo: move to screen?
std::shared_ptr<char> TermImpl::getsel()
{
    char *str, *ptr;
    int row, bufsize, lastcol, llen;
    screen::Glyph *gp, *last;

    const auto& sel = m_screen.sel();
    if (sel.empty())
        return nullptr;

    bufsize = (m_screen.cols() + 1) * (sel.ne.row - sel.nb.row + 1) * utf_size;
    // todo: look at using std::array or vector instead, w/ move
    ptr = str = new char[bufsize];

    // append every set & selected glyph to the selection
    for (row = sel.nb.row; row <= sel.ne.row; row++) {
        if ((llen = m_screen.linelen(row)) == 0) {
            *ptr++ = '\n';
            continue;
        }

        if (sel.rectangular()) {
            gp = &m_screen.glyph({row, sel.nb.col});
            lastcol = sel.ne.col;
        } else {
            gp = &m_screen.glyph({row, sel.nb.row == row ? sel.nb.col : 0});
            lastcol = (sel.ne.row == row) ? sel.ne.col : m_screen.cols() - 1;
        }
        last = &m_screen.glyph({row, std::min(lastcol, llen - 1)});
        while (last >= gp && last->u == screen::empty_char)
            --last;

        for (; gp <= last; ++gp) {
            if (gp->attr[screen::ATTR_WDUMMY])
                continue;

            ptr += utf8encode(gp->u, ptr);
        }

        // use \n for line ending in outgoing data
        if ((row < sel.ne.row || lastcol >= llen) &&
                !(last->attr[screen::ATTR_WRAP]))
            *ptr++ = '\n';
    }
    *ptr = 0;

    return std::shared_ptr<char>(str, std::default_delete<char[]>());
}

Term::Term(std::shared_ptr<event::Bus> bus, int cols, int rows) :
    impl(std::make_unique<TermImpl>(std::move(bus), cols, rows))
{}

Term::~Term() = default;

void Term::setWindow(std::shared_ptr<Window> window)
{
    impl->setWindow(std::move(window));
}

void Term::setTty(std::shared_ptr<Tty> tty)
{
    impl->setTty(std::move(tty));
}

const screen::Glyph& Term::glyph(const Cell& cell) const
{
    return impl->glyph(cell);
}

screen::Glyph& Term::glyph(const Cell& cell)
{
    return impl->glyph(cell);
}

void Term::reset()
{
    impl->reset();
}

void Term::setprint()
{
    impl->setprint();
}

const term_mode& Term::mode() const
{
    return impl->mode();
}

void Term::blink()
{
    impl->blink();
}

const Selection& Term::sel() const
{
    return impl->sel();
}

const screen::Cursor& Term::cursor() const
{
    return impl->cursor();
}

screen::cursor_type Term::cursortype() const
{
    return impl->cursortype();
}

bool Term::isdirty(int row) const
{
    return impl->isdirty(row);
}

void Term::setdirty()
{
    impl->setdirty();
}

void Term::cleardirty(int row)
{
    impl->cleardirty(row);
}

void Term::putc(char32_t u)
{
    impl->putc(u);
}

void Term::mousereport(const Cell& cell, mouse_event_enum evt, int button,
        const keymod_state& mod)
{
    impl->mousereport(cell, evt, button, mod);
}

int Term::rows() const
{
    return impl->rows();
}

int Term::cols() const
{
    return impl->cols();
}

uint32_t Term::deffg() const
{
    return impl->deffg();
}

uint32_t Term::defbg() const
{
    return impl->defbg();
}

uint32_t Term::defcs() const
{
    return impl->defcs();
}

uint32_t Term::defrcs() const
{
    return impl->defrcs();
}

void Term::setfocused(bool focused)
{
    impl->setfocused(focused);
}

bool Term::focused() const
{
    return impl->focused();
}

void Term::selclear()
{
    impl->selclear();
}

void Term::clipcopy()
{
    impl->clipcopy();
}

void Term::send(const char* data, std::size_t len /* = 0 */)
{
    if (!len)
        len = std::strlen(data);
    impl->send(data, len);
}

} // namespace term
