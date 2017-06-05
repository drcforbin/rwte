#include <limits.h>
#include <cstdint>
#include <vector>
#include <time.h>

#include "fmt/format.h"

#include "rwte/config.h"
#include "rwte/rwte.h"
#include "rwte/logging.h"
#include "rwte/term.h"
#include "rwte/tty.h"
#include "rwte/utf8.h"
#include "rwte/window.h"
#include "rwte/luastate.h"
#include "rwte/luaterm.h"

#define LOGGER() (logging::get("term"))

#define MIN(a, b) ((a) < (b)? (a) : (b))
#define MAX(a, b) ((a) < (b)? (b) : (a))
#define LIMIT(x, a, b)  ((x) = (x) < (a) ? (a) : ((x) > (b) ? (b) : (x)))
#define DEFAULT(a, b)  (a) = (a) ? (a) : (b)

#define TIMEDIFF(t1, t2) \
        ((t1.tv_sec-t2.tv_sec)*1000 + \
         (t1.tv_nsec-t2.tv_nsec)/1E6)

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
    ESC_STR,  // OSC, PM, APC
    ESC_ALTCHARSET,
    ESC_STR_END, // a final string was encountered
    ESC_TEST, // enter in test mode
    ESC_UTF8,
    ESC_DCS,
    ESC_LAST = ESC_DCS
};

typedef std::bitset<ESC_LAST+1> escape_state;

enum cursor_movement
{
    CURSOR_SAVE,
    CURSOR_LOAD
};

enum cursor_state
{
    CURSOR_DEFAULT  = 0,
    CURSOR_WRAPNEXT = 1,
    CURSOR_ORIGIN   = 2
};

const int esc_buf_size = (128*utf_size);
const int esc_arg_size = 16;
const int str_buf_size = esc_buf_size;
const int str_arg_size = esc_arg_size;

// CSI Escape sequence structs
// ESC '[' [[ [<priv>] <arg> [;]] <mode> [<mode>]]
struct CSIEscape
{
    char buf[esc_buf_size]; // raw string
    int len;               // raw string length
    bool priv;
    int arg[esc_arg_size];
    int narg;              // num args
    char mode[2];
};

// STR Escape sequence structs
// ESC type [[ [<priv>] <arg> [;]] <mode>] ESC '\'
struct STREscape
{
    char type;             // ESC type ...
    char buf[str_buf_size]; // raw string
    int len;               // raw string length
    char *args[str_arg_size];
    int narg;              // nb of args
};

// default values to use if we don't have
// a default value in config
static const int DEFAULT_TAB_SPACES = 8;
static const int DEFAULT_DCLICK_TIMEOUT = 300;
static const int DEFAULT_TCLICK_TIMEOUT = 600;

static cursor_type get_cursor_type()
{
    auto L = rwte.lua();
    L->getglobal("config");
    L->getfield(-1, "cursor_type");
    std::string cursor_type = L->tostring(-1);
    L->pop(2);

    if (cursor_type == "blink block")
        return CURSOR_BLINK_BLOCK;
    else if (cursor_type == "steady block")
        return CURSOR_STEADY_BLOCK;
    else if (cursor_type == "blink under")
        return CURSOR_BLINK_UNDER;
    else if (cursor_type == "steady under")
        return CURSOR_STEADY_UNDER;
    else if (cursor_type == "blink bar")
        return CURSOR_BLINK_BAR;
    else if (cursor_type == "steady bar")
        return CURSOR_STEADY_BAR;
    else
        return CURSOR_STEADY_BLOCK;
}

static bool allow_alt_screen()
{
    // check options first
    if (options.noalt)
        return false;

    // option not set, check lua config
    auto L = rwte.lua();
    L->getglobal("config");
    L->getfield(-1, "allow_alt_screen");

    // if the field is missing, default to true
    bool allow;
    if (L->isnil(-1))
        allow = true;
    else
        allow = L->tobool(-1);

    L->pop(2);

    return allow;
}

class TermImpl
{
public:
    TermImpl(int cols, int rows);

    const Glyph& glyph(int col, int row) const { return m_lines[col][row]; }

    void reset();
    void resize(int cols, int rows);

    void setprint();

    const term_mode& mode() const { return m_mode; }

    void blink();

    const Selection& sel() const { return m_sel; }
    const Cursor& cursor() const { return m_cursor; }
    cursor_type cursortype() const { return m_cursortype; }

    bool isdirty(int row) const { return m_dirty[row]; }
    void setdirty() { setdirty(0, m_rows-1); }
    void cleardirty(int row) { m_dirty[row] = false; }

    void putc(Rune u);
    void mousereport(int col, int row, mouse_event_enum evt, int button,
            const keymod_state& mod);

    int rows() const { return m_rows; }
    int cols() const { return m_cols; }

    // default colors
    uint32_t deffg() const { return m_deffg; }
    uint32_t defbg() const { return m_defbg; }
    uint32_t defcs() const { return m_defcs; }
    uint32_t defrcs() const { return m_defrcs; }

    void setfocused(bool focused);
    bool focused() const { return m_focused; }

    void selclear();
    void clipcopy();

    void send(const char *data, std::size_t len);

private:
    void start_blink();

    void moveto(int col, int row);
    void moveato(int col, int row);
    void cursor(int mode);

    void newline(bool first_col);

    void setscroll(int t, int b);
    void scrollup(int orig, int n);
    void scrolldown(int orig, int n);

    void clearregion(int col1, int row1, int col2, int row2);
    void deletechar(int n);
    void deleteline(int n);
    void insertblank(int n);
    void insertblankline(int n);
    void swapscreen();

    void setdirty(int top, int bot);

    bool selected(int col, int row);
    void selsnap(int *col, int *row, int direction);

    void setchar(Rune u, const Glyph& attr, int col, int row);
    void defutf8(char ascii);
    void selscroll(int orig, int n);
    void selnormalize();
    int linelen(int row);
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
    void setattr(int *attr, int l);
    void settmode(bool priv, bool set, int *args, int narg);
    void getbuttoninfo(int col, int row, const keymod_state& mod);
    char *getsel();

    term_mode m_mode; // terminal mode
    escape_state m_esc; // escape mode
    int m_rows, m_cols; // size
    std::vector<std::vector<Glyph>> m_lines;     // screen
    std::vector<std::vector<Glyph>> m_alt_lines; // alternate screen
    std::vector<bool> m_dirty;  // dirtyness of lines
    std::vector<bool> m_tabs; // false where no tab, true where tab
    Cursor m_cursor;
    Cursor m_stored_cursors[2];
    cursor_type m_cursortype;
    int m_top, m_bot; // scroll limits
    Selection m_sel;
    CSIEscape m_csiesc;
    STREscape m_stresc;

    int m_charset;  // current charset
    int m_icharset; // selected charset for sequence
    bool m_numlock; // you know, numlock
    bool m_focused; // whether terminal has focus

    char m_trantbl[4]; // charset table translation
    uint32_t m_deffg, m_defbg, m_defcs, m_defrcs; // default colors
};


TermImpl::TermImpl(int cols, int rows) :
    m_rows(0), m_cols(0),
    m_numlock(true),
    m_focused(false)
{
    Cursor c{};
    m_cursor = c;

    strreset();
    csireset();

    memset(&m_sel, 0, sizeof(m_sel));
    m_sel.mode = SEL_IDLE;
    m_sel.ob.col = -1;

    // only a few things are initialized here
    // the rest happens in resize and reset
    resize(cols, rows);
    reset();
}

void TermImpl::reset()
{
    unsigned int i;
    int isnum = 0;

    // get config
    auto L = rwte.lua();
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

    Cursor c{};
    c.attr.fg = default_fg;
    c.attr.bg = default_bg;

    m_cursor = c;
    m_stored_cursors[0] = c;
    m_stored_cursors[1] = c;

    m_cursortype = get_cursor_type();

    m_deffg = default_fg;
    m_defbg = default_bg;
    m_defcs = default_cs;
    m_defrcs = default_rcs;

    for (i = 0; i < m_tabs.size(); i++)
    {
        if (i == 0 || i % tab_spaces != 0)
            m_tabs[i] = false;
        else
            m_tabs[i] = true;
    }

    m_top = 0;
    m_bot = m_rows - 1;

    bool printing = m_mode[MODE_PRINT];
    m_mode.reset();
    m_mode.set(MODE_WRAP);
    m_mode.set(MODE_UTF8);
    if (printing)
        m_mode.set(MODE_PRINT);

    m_esc.reset();
    memset(m_trantbl, CS_USA, sizeof(m_trantbl));
    m_charset = 0;
    m_icharset = 0;

    for (i = 0; i < 2; i++)
    {
        moveto(0, 0);
        cursor(CURSOR_SAVE);
        clearregion(0, 0, m_cols-1, m_rows-1);
        swapscreen();
    }

    if (m_cursortype == CURSOR_BLINK_BLOCK ||
            m_cursortype == CURSOR_BLINK_UNDER ||
            m_cursortype == CURSOR_BLINK_BAR)
        start_blink();
}

void TermImpl::resize(int cols, int rows)
{
    LOGGER()->info("resize to {}x{}", cols, rows);

    int minrow = MIN(rows, m_rows);
    int mincol = MIN(cols, m_cols);

    if (cols < 1 || rows < 1) {
        LOGGER()->error("attempted resize to {}x{}", cols, rows);
        return;
    }

    // slide screen to keep cursor where we expect it
    if (m_cursor.row - rows >= 0)
    {
        LOGGER()->debug("removing {} lines for cursor", (m_cursor.row - rows) + 1);
        m_lines.erase(m_lines.begin(), m_lines.begin() + (m_cursor.row - rows) + 1);
        m_alt_lines.erase(m_alt_lines.begin(), m_alt_lines.begin() + (m_cursor.row - rows) + 1);
    }

    // resize to new height
    m_lines.resize(rows);
    m_alt_lines.resize(rows);
    m_dirty.resize(rows);
    m_tabs.resize(cols);

    // resize each row to new width, zero-pad if needed
    int i;
    for (i = 0; i < minrow; i++)
    {
        m_lines[i].resize(cols);
        m_alt_lines[i].resize(cols);
    }

    // allocate any new rows
    for (; i < rows; i++)
    {
        m_lines[i].resize(cols);
        m_alt_lines[i].resize(cols);
    }

    if (cols > m_cols)
    {
        auto L = rwte.lua();
        L->getglobal("config");
        L->getfield(-1, "tab_spaces");
        int tab_spaces = L->tointegerdef(-1, DEFAULT_TAB_SPACES);
        L->pop(2);

        // point to end of old size
        auto bp = m_tabs.begin() + m_cols;
        // back up to last tab (or begin)
        while (--bp != m_tabs.begin() && !*bp) {}
        // set tabs from here (resize cleared newly added tabs)
        auto idx = bp - m_tabs.begin();
        for ( idx += tab_spaces; idx < m_tabs.size(); idx += tab_spaces)
            m_tabs[idx] = true;
    }

    // update terminal size
    m_cols = cols;
    m_rows = rows;

    // reset scrolling region
    setscroll(0, rows-1);
    // make use of the LIMIT in moveto
    moveto(m_cursor.col, m_cursor.row);

    // store cursor
    Cursor c = m_cursor;
    // clear both screens (dirties all lines)
    for (i = 0; i < 2; i++)
    {
        if (mincol < cols && 0 < minrow)
            clearregion(mincol, 0, cols - 1, minrow - 1);
        if (0 < cols && minrow < rows)
            clearregion(0, minrow, cols - 1, rows - 1);
        swapscreen();
        cursor(CURSOR_LOAD);
    }
    // reset cursor
    m_cursor = c;
}

void TermImpl::setprint()
{
    m_mode.set(MODE_PRINT);
}

void TermImpl::blink()
{
    bool need_blink = m_cursortype == CURSOR_BLINK_BLOCK ||
            m_cursortype == CURSOR_BLINK_UNDER ||
            m_cursortype == CURSOR_BLINK_BAR;

    // see if we have anything blinking and mark blinking lines dirty
    for (int i = 0; i < m_lines.size(); i++)
    {
        auto& line = m_lines[i];
        for (auto& g : line)
        {
            if (g.attr[ATTR_BLINK])
            {
                need_blink = true;
                setdirty(i, i);
                break;
            }
        }
    }

    if (need_blink)
    {
        // toggle blink
        m_mode[MODE_BLINK] = !m_mode[MODE_BLINK];
    }
    else
    {
        // reset and stop blinking
        m_mode[MODE_BLINK] = false;
        rwte.stop_blink();
    }

    rwte.refresh();
}

void TermImpl::start_blink()
{
    // reset mode every time this is called, so that the
    // cursor shows while the screen is being updated
    m_mode[MODE_BLINK] = false;
    rwte.start_blink();
}

void TermImpl::moveto(int col, int row)
{
    int minrow, maxrow;
    if (m_cursor.state & CURSOR_ORIGIN)
    {
        minrow = m_top;
        maxrow = m_bot;
    }
    else
    {
        minrow = 0;
        maxrow = m_rows - 1;
    }

    m_cursor.state &= ~CURSOR_WRAPNEXT;
    m_cursor.col = LIMIT(col, 0, m_cols-1);
    m_cursor.row = LIMIT(row, minrow, maxrow);

    rwte.refresh();
}

// for absolute user moves, when decom is set
void TermImpl::moveato(int col, int row)
{
    moveto(col, row + ((m_cursor.state & CURSOR_ORIGIN) ? m_top: 0));
}

void TermImpl::cursor(int mode)
{
    int alt = m_mode[MODE_ALTSCREEN] ? 1 : 0;

    if (mode == CURSOR_SAVE)
    {
        m_stored_cursors[alt] = m_cursor;
    }
    else if (mode == CURSOR_LOAD)
    {
        m_cursor = m_stored_cursors[alt];
        moveto(m_cursor.col, m_cursor.row);
    }
}

static bool iscontrolc0(Rune c)
{
    return c <= 0x1f || c == '\177';
}

static bool iscontrolc1(Rune c)
{
    return 0x80 <= c && c <= 0x9f;
}

static bool iscontrol(Rune c)
{
    return iscontrolc0(c) || iscontrolc1(c);
}

static bool isdelim(Rune c)
{
    auto L = rwte.lua();
    L->getglobal("config");
    L->getfield(-1, "word_delimiters");
    const char * word_delimiters = L->tostring(-1);

    // if word_delimiters is missing, it'll select whole line
    bool delim = false;
    if (word_delimiters)
        delim = utf8strchr(word_delimiters, c) != NULL;

    L->pop(2);
    return delim;
}

void TermImpl::putc(Rune u)
{
    char c[utf_size];
    int width, len;

    bool control = iscontrol(u);

    // not UTF8 or SIXEL
    if (!m_mode[MODE_UTF8] && !m_mode[MODE_SIXEL])
    {
        c[0] = u;
        width = len = 1;
    }
    else
    {
        len = utf8encode(u, c);
        if (!control && (width = wcwidth(u)) == -1)
        {
            memcpy(c, "\357\277\275", 4); /* UTF_INVALID */
            width = 1;
        }
    }

    if (m_mode[MODE_PRINT])
        g_tty->print(c, len);

    // STR sequence must be checked before anything else
    // because it uses all following characters until it
    // receives a ESC, a SUB, a ST or any other C1 control
    // character.
    if (m_esc[ESC_STR])
    {
        if (u == '\a' || u == 030 || u == 032 || u == 033 ||
                iscontrolc1(u))
        {
            m_esc.reset(ESC_START);
            m_esc.reset(ESC_STR);
            m_esc.reset(ESC_DCS);
            if (m_mode[MODE_SIXEL])
            {
                // TODO: render sixel
                m_mode.reset(MODE_SIXEL);
                return;
            }
            m_esc.set(ESC_STR_END);
            goto check_control_code;
        }


        if (m_mode[MODE_SIXEL])
        {
            // TODO: implement sixel mode
            return;
        }

        if (m_esc[ESC_DCS] && m_stresc.len == 0 && u == 'q')
            m_mode.set(MODE_SIXEL);

        if (m_stresc.len+len >= sizeof(m_stresc.buf)-1)
        {
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

        memmove(&m_stresc.buf[m_stresc.len], c, len);
        m_stresc.len += len;
        return;
    }

check_control_code:
    // Actions of control codes must be performed as soon they arrive
    // because they can be embedded inside a control sequence, and
    // they must not cause conflicts with sequences.
    if (control)
    {
        controlcode(u);

        // control codes are not shown ever
        return;
    }
    else if (m_esc[ESC_START])
    {
        if (m_esc[ESC_CSI])
        {
            m_csiesc.buf[m_csiesc.len++] = u;
            if ((0x40 <= u && u <= 0x7E) ||
                    m_csiesc.len >= sizeof(m_csiesc.buf)-1)
            {
                m_esc.reset();
                csiparse();
                csihandle();
            }
            return;
        }
        else if (m_esc[ESC_UTF8])
            defutf8(u);
        else if (m_esc[ESC_ALTCHARSET])
            deftran(u);
        else if (m_esc[ESC_TEST])
            dectest(u);
        else
        {
            if (!eschandle(u))
                return;

            // sequence already finished
        }

        m_esc.reset();

        // don't print sequence chars
        return;
    }

    if (m_sel.ob.col != -1 &&
            m_sel.ob.row <= m_cursor.row && m_cursor.row <= m_sel.oe.row)
        selclear();

    Glyph *gp = &m_lines[m_cursor.row][m_cursor.col];
    if (m_mode[MODE_WRAP] && (m_cursor.state & CURSOR_WRAPNEXT))
    {
        gp->attr.set(ATTR_WRAP);
        newline(1);
        gp = &m_lines[m_cursor.row][m_cursor.col];
    }

    if (m_mode[MODE_INSERT] && m_cursor.col+width < m_cols)
        memmove(gp+width, gp, (m_cols - m_cursor.col - width) * sizeof(Glyph));

    if (m_cursor.col+width > m_cols)
    {
        newline(1);
        gp = &m_lines[m_cursor.row][m_cursor.col];
    }

    setchar(u, m_cursor.attr, m_cursor.col, m_cursor.row);

    if (width == 2)
    {
        gp->attr.set(ATTR_WIDE);
        if (m_cursor.col+1 < m_cols)
        {
            gp[1].u = '\0';
            gp[1].attr.reset();
            gp[1].attr.set(ATTR_WDUMMY);
        }
    }

    if (m_cursor.col+width < m_cols)
        moveto(m_cursor.col+width, m_cursor.row);
    else
        m_cursor.state |= CURSOR_WRAPNEXT;
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
    MOUSEFLAGS_BUTTON5
};

void TermImpl::mousereport(int col, int row, mouse_event_enum evt, int button,
        const keymod_state& mod)
{
    // todo: suspicious use of static
    static int oldbutton = MOUSEFLAGS_RELEASE;
    static int ocol, orow;

    if (evt == MOUSE_PRESS || evt == MOUSE_RELEASE)
    {
        if (button < 1 || 5 < button)
        {
            LOGGER()->error("button event {} for unexpected button {}", evt, button);
            return;
        }
    }

    if (LOGGER()->level() <= logging::trace)
    {
        std::string mode;
        if (m_mode[MODE_MOUSEBTN])
            mode = "BTN";
        if (m_mode[MODE_MOUSEMOTION])
        {
            if (!mode.empty())
                mode += ",";
            mode += "MOT";
        }
        if (m_mode[MODE_MOUSEX10])
        {
            if (!mode.empty())
                mode += ",";
            mode += "X10";
        }
        if (m_mode[MODE_MOUSEMANY])
        {
            if (!mode.empty())
                mode += ",";
            mode += "MNY";
        }

        if (evt == MOUSE_MOTION)
        {
            LOGGER()->trace("mousereport MOTION {}, {}, oldbutton={}, mode={}",
                col, row, oldbutton, mode);
        }
        else
        {
            LOGGER()->trace("mousereport {} {}, {}, {}, oldbutton={}, mode={}",
                evt == MOUSE_PRESS? "PRESS" : "RELEASE",
                button, col, row, oldbutton, mode);
        }
    }

    // if FORCE_SEL_MOD is set and all modifiers are present
    bool forcesel = mod != EMPTY_MASK && (mod & FORCE_SEL_MOD) == FORCE_SEL_MOD;

    if ((m_mode & mouse_modes).any() && !forcesel)
    {
        // bitfield for buttons
        int cb;

        // from st, who got it from urxvt
        if (evt == MOUSE_MOTION)
        {
            // return if we haven't moved
            if (col == ocol && row == orow)
                return;

            // motion is only reported in MOUSEMOTION and MOUSEMANY
            if (!m_mode[MODE_MOUSEMOTION] && !m_mode[MODE_MOUSEMANY])
                return;

            // MOUSEMOTION only reports when a button is pressed
            if (m_mode[MODE_MOUSEMOTION] && oldbutton == MOUSEFLAGS_RELEASE)
                return;

            cb = oldbutton | MOUSEFLAGS_MOTION;

            ocol = col;
            orow = row;
        }
        else
        {
            if (!m_mode[MODE_MOUSESGR] && evt == MOUSE_RELEASE)
                cb = MOUSEFLAGS_RELEASE;
            else
                cb = button_map[button - 1]; // look up the button

            if (evt == MOUSE_PRESS)
            {
                oldbutton = cb;
                ocol = col;
                orow = row;
            }
            else if (evt == MOUSE_RELEASE)
            {
                oldbutton = MOUSEFLAGS_RELEASE;

                // MODE_MOUSEX10: no button release reporting
                if (m_mode[MODE_MOUSEX10])
                    return;

                // release events are not reported for mousewheel buttons
                if (button == 4 || button == 5)
                    return;
            }
        }

        if (!m_mode[MODE_MOUSEX10])
        {
            // except for X10 mode, when reporting
            // button we need to include shift, ctrl, meta/logo

            // probably never get shift
            cb |= (mod[MOD_SHIFT]? MOUSEFLAGS_SHIFT : 0) |
                    (mod[MOD_LOGO]? MOUSEFLAGS_LOGO  : 0) |
                    (mod[MOD_CTRL]? MOUSEFLAGS_CTRL : 0);
        }

        if (m_mode[MODE_MOUSESGR])
        {
            std::string seq = fmt::format("\033[<{};{};{}{:c}",
                    cb, col+1, row+1, (evt == MOUSE_RELEASE)? 'm' : 'M');
            g_tty->write(seq.c_str(), seq.size());
        }
        else if (col < 223 && row < 223)
        {
            std::string seq = fmt::format("\033[M{:c}{:c}{:c}",
                    (char) (32+cb), (char) (32+col+1), (char) (32+row+1));
            g_tty->write(seq.c_str(), seq.size());
        }
        else
        {
            // row or col is out of range...can't report unless
            // we're in MOUSESGR
            return;
        }
    }
    else
    {
        if (evt == MOUSE_PRESS)
        {
            auto L = rwte.lua();

            if (luaterm_mouse_press(L.get(), col, row, button, mod))
                return;

            // start selection?
            if (button == 1)
            {
                L->getglobal("config");

                L->getfield(-1, "dclick_timeout");
                int dclick_timeout = L->tointegerdef(-1, DEFAULT_DCLICK_TIMEOUT);

                L->getfield(-2, "tclick_timeout");
                int tclick_timeout = L->tointegerdef(-1, DEFAULT_TCLICK_TIMEOUT);

                L->pop(3);

                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);

                // clear previous selection, logically and visually.
                selclear();
                m_sel.mode = SEL_EMPTY;
                m_sel.type = SEL_REGULAR;
                m_sel.oe.col = m_sel.ob.col = col;
                m_sel.oe.row = m_sel.ob.row = row;

                // if the user clicks below predefined timeouts specific
                // snapping behaviour is exposed.
                if (TIMEDIFF(now, m_sel.tclick2) <= tclick_timeout)
                    m_sel.snap = SNAP_LINE;
                else if (TIMEDIFF(now, m_sel.tclick1) <= dclick_timeout)
                    m_sel.snap = SNAP_WORD;
                else
                    m_sel.snap = 0;

                selnormalize();

                if (m_sel.snap != 0)
                    m_sel.mode = SEL_READY;
                setdirty(m_sel.nb.row, m_sel.ne.row);
                m_sel.tclick2 = m_sel.tclick1;
                m_sel.tclick1 = now;
            }
        }
        else if (evt == MOUSE_RELEASE)
        {
            if (button == 2)
                window.selpaste();
            else if (button == 1)
            {
                if (m_sel.mode == SEL_READY)
                {
                    getbuttoninfo(col, row, mod);

                    // todo: better data type
                    char * sel = getsel();
                    // setsel assumes ownership
                    window.setsel(sel);
                }
                else
                    selclear();
                m_sel.mode = SEL_IDLE;
                setdirty(m_sel.nb.row, m_sel.ne.row);
            }
        }
        else if (evt == MOUSE_MOTION)
        {
            if (!m_sel.mode)
                return;

            m_sel.mode = SEL_READY;
            int olderow = m_sel.oe.row;
            int oldecol = m_sel.oe.col;
            int oldsbrow = m_sel.nb.row;
            int oldserow = m_sel.ne.row;
            getbuttoninfo(col, row, mod);

            if (olderow != m_sel.oe.row || oldecol != m_sel.oe.col)
                setdirty(MIN(m_sel.nb.row, oldsbrow), MAX(m_sel.ne.row, oldserow));
        }
    }
}

void TermImpl::newline(bool first_col)
{
    int y = m_cursor.row;

    if (y == m_bot)
        scrollup(m_top, 1);
    else
        y++;

    moveto(first_col ? 0 : m_cursor.col, y);
}

void TermImpl::setscroll(int t, int b)
{
    LIMIT(t, 0, m_rows-1);
    LIMIT(b, 0, m_rows-1);

    if (t > b)
        std::swap(t, b);

    m_top = t;
    m_bot = b;
}

void TermImpl::scrollup(int orig, int n)
{
    LIMIT(n, 0, m_bot-orig+1);

    clearregion(0, orig, m_cols-1, orig+n-1);
    setdirty(orig+n, m_bot);

    for (int i = orig; i <= m_bot-n; i++)
        std::swap(m_lines[i], m_lines[i+n]);

    selscroll(orig, -n);
}

void TermImpl::scrolldown(int orig, int n)
{
    LIMIT(n, 0, m_bot-orig+1);

    setdirty(orig, m_bot-n);
    clearregion(0, m_bot-n+1, m_cols-1, m_bot);

    for (int i = m_bot; i >= orig+n; i--)
        std::swap(m_lines[i], m_lines[i-n]);

    selscroll(orig, n);
}

void TermImpl::clearregion(int col1, int row1, int col2, int row2)
{
    if (col1 > col2)
        std::swap(col1, col2);
    if (row1 > row2)
        std::swap(row1, row2);

    LIMIT(col1, 0, m_cols-1);
    LIMIT(col2, 0, m_cols-1);
    LIMIT(row1, 0, m_rows-1);
    LIMIT(row2, 0, m_rows-1);

    for (int row = row1; row <= row2; row++)
    {
        m_dirty[row] = true;
        for (int col = col1; col <= col2; col++)
        {
            Glyph& gp = m_lines[row][col];
            if (selected(col, row))
                selclear();
            gp.fg = m_cursor.attr.fg;
            gp.bg = m_cursor.attr.bg;
            gp.attr.reset();
            gp.u = ' ';
        }
    }
}

void TermImpl::deletechar(int n)
{
    LIMIT(n, 0, m_cols - m_cursor.col);

    int dst = m_cursor.col;
    int src = m_cursor.col + n;
    int size = m_cols - src;

    auto lineit = m_lines[m_cursor.row].begin();

    std::copy(lineit+src, lineit+src+size, lineit+dst);
    clearregion(m_cols-n, m_cursor.row, m_cols-1, m_cursor.row);
}

void TermImpl::deleteline(int n)
{
    if (m_top <= m_cursor.row && m_cursor.row <= m_bot)
        scrollup(m_cursor.row, n);
}

void TermImpl::insertblank(int n)
{
    LIMIT(n, 0, m_cols - m_cursor.col);
    if (n > 0)
    {
        // move things over
        auto& line = m_lines[m_cursor.row];
        std::copy_backward(
                line.begin()+m_cursor.col,
                line.end()-n,
                line.end());

        // clear moved area
        clearregion(m_cursor.col, m_cursor.row,
                m_cursor.col + n - 1, m_cursor.row);
    }
}

void TermImpl::insertblankline(int n)
{
    if (m_top <= m_cursor.row && m_cursor.row <= m_bot)
        scrolldown(m_cursor.row, n);
}

void TermImpl::swapscreen()
{
    std::swap(m_lines, m_alt_lines);

    m_mode.flip(MODE_ALTSCREEN);

    setdirty();
}

void TermImpl::setdirty(int top, int bot)
{
    LIMIT(top, 0, m_rows-1);
    LIMIT(bot, 0, m_rows-1);

    for (int i = top; i <= bot; i++)
        m_dirty[i] = true;

    rwte.refresh();
}

bool TermImpl::selected(int col, int row)
{
    if (m_sel.mode == SEL_EMPTY)
        return false;

    if (m_sel.type == SEL_RECTANGULAR)
        return (m_sel.nb.row <= row && row <= m_sel.ne.row) &&
                (m_sel.nb.col <= col && col <= m_sel.ne.col);

    return (m_sel.nb.row <= row && row <= m_sel.ne.row) &&
            (row != m_sel.nb.row || col >= m_sel.nb.col) &&
            (row != m_sel.ne.row || col <= m_sel.ne.col);
}

void TermImpl::selsnap(int *col, int *row, int direction)
{
    int newcol, newrow, colt, rowt;
    int delim, prevdelim;
    Glyph *gp, *prevgp;

    switch (m_sel.snap)
    {
    case SNAP_WORD:
        // Snap around if the word wraps around at the end or
        // beginning of a line.

        prevgp = &m_lines[*row][*col];
        prevdelim = isdelim(prevgp->u);
        for (;;)
        {
            newcol = *col + direction;
            newrow = *row;
            if (newcol < 0 || (m_cols - 1) < newcol)
            {
                newrow += direction;
                newcol = (newcol + m_cols) % m_cols;
                if (newrow < 0 || (m_rows - 1) < newrow)
                    break;

                if (direction > 0)
                    rowt = *row, colt = *col;
                else
                    rowt = newrow, colt = newcol;
                if (!m_lines[rowt][colt].attr[ATTR_WRAP])
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
    case SNAP_LINE:
        // Snap around if the the previous line or the current one
        // has set ATTR_WRAP at its end. Then the whole next or
        // previous line will be selected.

        *col = (direction < 0) ? 0 : m_cols - 1;
        if (direction < 0)
        {
            for (; *row > 0; *row += direction)
            {
                if (!m_lines[*row-1][m_cols-1].attr[ATTR_WRAP])
                {
                    break;
                }
            }
        }
        else if (direction > 0)
        {
            for (; *row < m_rows-1; *row += direction)
            {
                if (!m_lines[*row][m_cols-1].attr[ATTR_WRAP])
                {
                    break;
                }
            }
        }
        break;
    }
}

void TermImpl::setfocused(bool focused)
{
    m_focused = focused;

    if (m_mode[MODE_FOCUS])
    {
        if (m_focused)
            g_tty->write("\033[I", 3);
        else
            g_tty->write("\033[O", 3);
    }

    rwte.refresh();
}

void TermImpl::selclear()
{
    if (m_sel.ob.col == -1)
        return;
    m_sel.mode = SEL_IDLE;
    m_sel.ob.col = -1;
    setdirty(m_sel.nb.row, m_sel.ne.row);
}

// todo: move to window, calling exposed getsel
void TermImpl::clipcopy()
{
    // todo: better data type
    char * sel = getsel();
    // setclip assumes ownership
    window.setclip(sel);
}

void TermImpl::send(const char *data, std::size_t len)
{
    g_tty->write(data, len);
}

void TermImpl::setchar(Rune u, const Glyph& attr, int col, int row)
{
    const char *vt100_0[62] = { // 0x41 - 0x7e
        "↑", "↓", "→", "←", "█", "▚", "☃", // A - G
        0, 0, 0, 0, 0, 0, 0, 0, // H - O
        0, 0, 0, 0, 0, 0, 0, 0, // P - W
        0, 0, 0, 0, 0, 0, 0, " ", // X - _
        "◆", "▒", "␉", "␌", "␍", "␊", "°", "±", // ` - g
        "␤", "␋", "┘", "┐", "┌", "└", "┼", "⎺", // h - o
        "⎻", "─", "⎼", "⎽", "├", "┤", "┴", "┬", // p - w
        "│", "≤", "≥", "π", "≠", "£", "·", // x - ~
    };

    // The table is proudly stolen from st, where it was
    // stolen from rxvt.
    if (m_trantbl[m_charset] == CS_GRAPHIC0 &&
            (0x41 <= u && u <= 0x7e) && vt100_0[u - 0x41])
        utf8decode(vt100_0[u - 0x41], &u, utf_size);

    if (m_lines[row][col].attr[ATTR_WIDE])
    {
        if (col+1 < m_cols)
        {
            m_lines[row][col+1].u = ' ';
            m_lines[row][col+1].attr.reset(ATTR_WDUMMY);
        }
    }
    else if (m_lines[row][col].attr[ATTR_WDUMMY])
    {
        m_lines[row][col-1].u = ' ';
        m_lines[row][col-1].attr.reset(ATTR_WIDE);
    }

    m_dirty[row] = true;
    m_lines[row][col] = attr;
    m_lines[row][col].u = u;

    if (attr.attr[ATTR_BLINK])
        start_blink();

    rwte.refresh();
}

void TermImpl::defutf8(char ascii)
{
    if (ascii == 'G')
        m_mode.set(MODE_UTF8);
    else if (ascii == '@')
        m_mode.reset(MODE_UTF8);
}

void TermImpl::selscroll(int orig, int n)
{
    if (m_sel.ob.col == -1)
        return;

    if ((orig <= m_sel.ob.row && m_sel.ob.row <= m_bot) ||
            (orig <= m_sel.oe.row && m_sel.oe.row <= m_bot))
    {
        if ((m_sel.ob.row += n) > m_bot || (m_sel.oe.row += n) < m_top)
        {
            selclear();
            return;
        }
        if (m_sel.type == SEL_RECTANGULAR)
        {
            if (m_sel.ob.row < m_top)
                m_sel.ob.row = m_top;
            if (m_sel.oe.row > m_bot)
                m_sel.oe.row = m_bot;
        }
        else
        {
            if (m_sel.ob.row < m_top)
            {
                m_sel.ob.row = m_top;
                m_sel.ob.col = 0;
            }
            if (m_sel.oe.row > m_bot)
            {
                m_sel.oe.row = m_bot;
                m_sel.oe.col = m_cols;
            }
        }
        selnormalize();
    }
}

void TermImpl::selnormalize()
{
    if (m_sel.type == SEL_REGULAR && m_sel.ob.row != m_sel.oe.row)
    {
        m_sel.nb.col = m_sel.ob.row < m_sel.oe.row ? m_sel.ob.col : m_sel.oe.col;
        m_sel.ne.col = m_sel.ob.row < m_sel.oe.row ? m_sel.oe.col : m_sel.ob.col;
    }
    else
    {
        m_sel.nb.col = MIN(m_sel.ob.col, m_sel.oe.col);
        m_sel.ne.col = MAX(m_sel.ob.col, m_sel.oe.col);
    }
    m_sel.nb.row = MIN(m_sel.ob.row, m_sel.oe.row);
    m_sel.ne.row = MAX(m_sel.ob.row, m_sel.oe.row);

    selsnap(&m_sel.nb.col, &m_sel.nb.row, -1);
    selsnap(&m_sel.ne.col, &m_sel.ne.row, +1);

    // expand selection over line breaks
    if (m_sel.type == SEL_RECTANGULAR)
        return;
    int i = linelen(m_sel.nb.row);
    if (i < m_sel.nb.col)
        m_sel.nb.col = i;
    if (linelen(m_sel.ne.row) <= m_sel.ne.col)
        m_sel.ne.col = m_cols - 1;
}

int TermImpl::linelen(int row)
{
    int i = m_cols;

    if (m_lines[row][i - 1].attr[ATTR_WRAP])
        return i;

    while (i > 0 && m_lines[row][i - 1].u == ' ')
        --i;

    return i;
}

void TermImpl::deftran(char ascii)
{
    const char cs[] = "0B";
    const int vcs[] = {CS_GRAPHIC0, CS_USA};

    char *p;
    if ((p = strchr(cs, ascii)) == NULL)
        LOGGER()->error("esc unhandled charset: ESC ( {}", ascii);
    else
        m_trantbl[m_icharset] = vcs[p - cs];
}

void TermImpl::dectest(char c)
{
    int col, row;

    if (c == '8')
    {
        // DEC screen alignment test
        for (col = 0; col < m_cols; ++col)
        {
            for (row = 0; row < m_rows; ++row)
                setchar('E', m_cursor.attr, col, row);
        }
    }
}

void TermImpl::controlcode(unsigned char ascii)
{
    // LOGGER()->trace("ctrlcode {:02X}", (int) ascii);

    switch (ascii) {
    case '\t':   // HT
        puttab(1);
        return;
    case '\b':   // BS
        moveto(m_cursor.col-1, m_cursor.row);
        return;
    case '\r':   // CR
        moveto(0, m_cursor.row);
        return;
    case '\f':   // LF
    case '\v':   // VT
    case '\n':   // LF
        // go to first col if the mode is set
        newline(m_mode[MODE_CRLF]);
        return;
    case '\a':   // BEL
        if (m_esc[ESC_STR_END])
        {
            // backwards compatibility to xterm
            strhandle();
        }
        else
        {
            if (!m_focused)
                window.seturgent(true);

            // default bell_volume to 0 if invalid
            auto L = rwte.lua();
            L->getglobal("config");
            L->getfield(-1, "bell_volume");
            int bell_volume = L->tointegerdef(-1, 0);
            LIMIT(bell_volume, -100, 100);
            L->pop(2);

            if (bell_volume)
                window.bell(bell_volume);
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
        setchar('?', m_cursor.attr, m_cursor.col, m_cursor.row);
    case '\030': // CAN
        csireset();
        break;
    case '\005': // ENQ (IGNORED)
    case '\000': // NUL (IGNORED)
    case '\021': // XON (IGNORED)
    case '\023': // XOFF (IGNORED)
    case 0177:   // DEL (IGNORED)
        return;
    case 0x80:   // TODO: PAD
    case 0x81:   // TODO: HOP
    case 0x82:   // TODO: BPH
    case 0x83:   // TODO: NBH
    case 0x84:   // TODO: IND
        break;
    case 0x85:   // NEL -- Next line
        newline(1); // always go to first col
        break;
    case 0x86:   // TODO: SSA
    case 0x87:   // TODO: ESA
        break;
    case 0x88:   // HTS -- Horizontal tab stop
        m_tabs[m_cursor.col] = true;
        break;
    case 0x89:   // TODO: HTJ
    case 0x8a:   // TODO: VTS
    case 0x8b:   // TODO: PLD
    case 0x8c:   // TODO: PLU
    case 0x8d:   // TODO: RI
    case 0x8e:   // TODO: SS2
    case 0x8f:   // TODO: SS3
    case 0x91:   // TODO: PU1
    case 0x92:   // TODO: PU2
    case 0x93:   // TODO: STS
    case 0x94:   // TODO: CCH
    case 0x95:   // TODO: MW
    case 0x96:   // TODO: SPA
    case 0x97:   // TODO: EPA
    case 0x98:   // TODO: SOS
    case 0x99:   // TODO: SGCI
        break;
    case 0x9a:   // DECID -- Identify Terminal
        {
            auto L = rwte.lua();
            L->getglobal("config");
            L->getfield(-1, "term_id");
            const char * term_id = L->tostring(-1);
            g_tty->write(term_id, std::strlen(term_id));
            L->pop(2);
        }
        break;
    case 0x9b:   // TODO: CSI
    case 0x9c:   // TODO: ST
        break;
    case 0x90:   // DCS -- Device Control String
    case 0x9d:   // OSC -- Operating System Command
    case 0x9e:   // PM -- Privacy Message
    case 0x9f:   // APC -- Application Program Command
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
        if (m_cursor.row == m_bot)
            scrollup(m_top, 1);
        else
            moveto(m_cursor.col, m_cursor.row+1);
        break;
    case 'E': // NEL -- Next line
        newline(1); // always go to first col
        break;
    case 'H': // HTS -- Horizontal tab stop
        m_tabs[m_cursor.col] = true;
        break;
    case 'M': // RI -- Reverse index
        if (m_cursor.row == m_top)
            scrolldown(m_top, 1);
        else
            moveto(m_cursor.col, m_cursor.row-1);
        break;
    case 'Z': // DECID -- Identify Terminal
        {
            auto L = rwte.lua();
            L->getglobal("config");
            L->getfield(-1, "term_id");
            const char * term_id = L->tostring(-1);
            g_tty->write(term_id, std::strlen(term_id));
            L->pop(2);
        }
        break;
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
        cursor(CURSOR_SAVE);
        break;
    case '8': // DECRC -- Restore Cursor
        cursor(CURSOR_LOAD);
        break;
    case '\\': // ST -- String Terminator
        if (m_esc[ESC_STR_END])
            strhandle();
        break;
    default:
        LOGGER()->error("unknown sequence ESC 0x{:02X} '{}'",
                (unsigned char) ascii, isprint(ascii)? ascii:'.');
        break;
    }
    return true;
}

void TermImpl::resettitle()
{
    window.settitle(options.title);
}

void TermImpl::puttab(int n)
{
    int col = m_cursor.col;

    if (n > 0)
    {
        while (col < m_cols && n--)
            for (++col; col < m_cols && !m_tabs[col]; ++col)
                ; // nothing
    }
    else if (n < 0)
    {
        while (col > 0 && n++)
            for (--col; col > 0 && !m_tabs[col]; --col)
                ; // nothing
    }

    m_cursor.col = LIMIT(col, 0, m_cols-1);
}

void TermImpl::strreset()
{
    memset(&m_stresc, 0, sizeof(m_stresc));
}

void TermImpl::strparse()
{
    int c;
    char *p = m_stresc.buf;

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

// todo: move me
int32_t hexcolor(const char *src)
{
    int32_t idx = -1;
    unsigned long val;
    char *e;

    size_t in_len = std::strlen(src);
    if (in_len == 7 && src[0] == '#')
    {
        if ((val = strtoul(src+1, &e, 16)) != ULONG_MAX && (e == src+7))
            idx = 1 << 24 | val;
        else
            LOGGER()->error("erresc: invalid hex color ({})", src);
    }
    else
        LOGGER()->error("erresc: short hex color ({})", src);

    return idx;
}

void TermImpl::strhandle()
{
    char *p = NULL;
    int j, narg, par;

    m_esc.reset(ESC_STR_END);
    m_esc.reset(ESC_STR);
    strparse();
    par = (narg = m_stresc.narg) ? atoi(m_stresc.args[0]) : 0;

    LOGGER()->trace("strhandle {}", strdump());

    switch (m_stresc.type)
    {
    case ']': // OSC -- Operating System Command
        switch (par) {
        case 0:
        case 1:
        case 2:
            if (narg > 1)
                window.settitle(m_stresc.args[1]);
            return;
        case 11:
            if (narg > 1)
            {
                int32_t color;
                if ((color = hexcolor(m_stresc.args[1])) >= 0)
                {
                    m_defbg = (uint32_t) color;
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
            if (narg > 2)
            {
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
            p = m_stresc.args[2];
            // FALLTHROUGH
        case 104: // color reset, here p = NULL
            j = (narg > 1) ? atoi(m_stresc.args[1]) : -1;
            // todo: remove dump
            LOGGER()->debug("OSC 4/104: {}", strdump());
            /*
            todo: color
            if (xsetcolorname(j, p)) {
                fprintf(stderr, "erresc: invalid color %s\n", p);
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
        window.settitle(m_stresc.args[0]);
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
    fmt::MemoryWriter msg;
    msg << "ESC" << m_stresc.type;

    for (int i = 0; i < m_stresc.len; i++)
    {
        unsigned int c = m_stresc.buf[i] & 0xff;
        if (c == '\0')
            return msg.str(); // early exit
        else if (isprint(c))
            msg << (char) c;
        else if (c == '\n')
            msg << "(\\n)";
        else if (c == '\r')
            msg << "(\\r)";
        else if (c == 0x1b)
            msg << "(\\e)";
        else
            msg.write("(0x{:02X})", c);
    }

    msg << "ESC\\\n";
    return msg.str();
}

void TermImpl::strsequence(unsigned char c)
{
    strreset();

    switch (c) {
    case 0x90:   // DCS -- Device Control String
        c = 'P';
        m_esc.set(ESC_DCS);
        break;
    case 0x9f:   // APC -- Application Program Command
        c = '_';
        break;
    case 0x9e:   // PM -- Privacy Message
        c = '^';
        break;
    case 0x9d:   // OSC -- Operating System Command
        c = ']';
        break;
    }

    m_stresc.type = c;
    m_esc.set(ESC_STR);
}

void TermImpl::csireset()
{
    memset(&m_csiesc, 0, sizeof(m_csiesc));
}

void TermImpl::csiparse()
{
    char *p = m_csiesc.buf, *np;
    long int v;

    m_csiesc.narg = 0;
    if (*p == '?')
    {
        m_csiesc.priv = true;
        p++;
    }

    m_csiesc.buf[m_csiesc.len] = '\0';
    while (p < m_csiesc.buf+m_csiesc.len)
    {
        np = NULL;
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
    m_csiesc.mode[1] = (p < m_csiesc.buf+m_csiesc.len) ? *p : '\0';
}

void TermImpl::csihandle()
{
    LOGGER()->trace("csiesc {}", csidump());

    switch (m_csiesc.mode[0])
    {
    case '@': // ICH -- Insert <n> blank char
        DEFAULT(m_csiesc.arg[0], 1);
        insertblank(m_csiesc.arg[0]);
        break;
    case 'A': // CUU -- Cursor <n> Up
        DEFAULT(m_csiesc.arg[0], 1);
        moveto(m_cursor.col, m_cursor.row-m_csiesc.arg[0]);
        break;
    case 'B': // CUD -- Cursor <n> Down
    case 'e': // VPR --Cursor <n> Down
        DEFAULT(m_csiesc.arg[0], 1);
        moveto(m_cursor.col, m_cursor.row+m_csiesc.arg[0]);
        break;
    case 'i': // MC -- Media Copy
        switch (m_csiesc.arg[0]) {
        /*
        todo: case media copy
        case 0:
            tdump();
            break;
        case 1:
            tdumpline(m_cursor.row);
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
        if (m_csiesc.arg[0] == 0)
        {
            auto L = rwte.lua();
            L->getglobal("config");
            L->getfield(-1, "term_id");
            const char * term_id = L->tostring(-1);
            g_tty->write(term_id, std::strlen(term_id));
            L->pop(2);
        }
        break;
    case 'C': // CUF -- Cursor <n> Forward
    case 'a': // HPR -- Cursor <n> Forward
        DEFAULT(m_csiesc.arg[0], 1);
        moveto(m_cursor.col+m_csiesc.arg[0], m_cursor.row);
        break;
    case 'D': // CUB -- Cursor <n> Backward
        DEFAULT(m_csiesc.arg[0], 1);
        moveto(m_cursor.col-m_csiesc.arg[0], m_cursor.row);
        break;
    case 'E': // CNL -- Cursor <n> Down and first col
        DEFAULT(m_csiesc.arg[0], 1);
        moveto(0, m_cursor.row+m_csiesc.arg[0]);
        break;
    case 'F': // CPL -- Cursor <n> Up and first col
        DEFAULT(m_csiesc.arg[0], 1);
        moveto(0, m_cursor.row-m_csiesc.arg[0]);
        break;
    case 'g': // TBC -- Tabulation clear
        switch (m_csiesc.arg[0])
        {
        case 0: // clear current tab stop
            m_tabs[m_cursor.col] = 0;
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
        DEFAULT(m_csiesc.arg[0], 1);
        moveto(m_csiesc.arg[0]-1, m_cursor.row);
        break;
    case 'H': // CUP -- Move to <row> <col>
    case 'f': // HVP
        DEFAULT(m_csiesc.arg[0], 1);
        DEFAULT(m_csiesc.arg[1], 1);
        moveato(m_csiesc.arg[1]-1, m_csiesc.arg[0]-1);
        break;
    case 'I': // CHT -- Cursor Forward Tabulation <n> tab stops
        DEFAULT(m_csiesc.arg[0], 1);
        puttab(m_csiesc.arg[0]);
        break;
    case 'J': // ED -- Clear screen
        selclear();
        switch (m_csiesc.arg[0])
        {
        case 0: // below
            clearregion(m_cursor.col, m_cursor.row, m_cols-1, m_cursor.row);
            if (m_cursor.row < m_rows-1)
                clearregion(0, m_cursor.row+1, m_cols-1, m_rows-1);
            break;
        case 1: // above
            if (m_cursor.row > 1)
                clearregion(0, 0, m_cols-1, m_cursor.row-1);
            clearregion(0, m_cursor.row, m_cursor.col, m_cursor.row);
            break;
        case 2: // all
            clearregion(0, 0, m_cols-1, m_rows-1);
            break;
        default:
            goto unknown;
        }
        break;
    case 'K': // EL -- Clear line
        switch (m_csiesc.arg[0])
        {
        case 0: // right
            clearregion(m_cursor.col, m_cursor.row, m_cols-1, m_cursor.row);
            break;
        case 1: // left
            clearregion(0, m_cursor.row, m_cursor.col, m_cursor.row);
            break;
        case 2: // all
            clearregion(0, m_cursor.row, m_cols-1, m_cursor.row);
            break;
        }
        break;
    case 'S': // SU -- Scroll <n> line up
        DEFAULT(m_csiesc.arg[0], 1);
        scrollup(m_top, m_csiesc.arg[0]);
        break;
    case 'T': // SD -- Scroll <n> line down
        DEFAULT(m_csiesc.arg[0], 1);
        scrolldown(m_top, m_csiesc.arg[0]);
        break;
    case 'L': // IL -- Insert <n> blank lines
        DEFAULT(m_csiesc.arg[0], 1);
        insertblankline(m_csiesc.arg[0]);
        break;
    case 'l': // RM -- Reset Mode
        settmode(m_csiesc.priv, false, m_csiesc.arg, m_csiesc.narg);
        break;
    case 'M': // DL -- Delete <n> lines
        DEFAULT(m_csiesc.arg[0], 1);
        deleteline(m_csiesc.arg[0]);
        break;
    case 'X': // ECH -- Erase <n> char
        DEFAULT(m_csiesc.arg[0], 1);
        clearregion(m_cursor.col, m_cursor.row,
                m_cursor.col + m_csiesc.arg[0] - 1, m_cursor.row);
        break;
    case 'P': // DCH -- Delete <n> char
        DEFAULT(m_csiesc.arg[0], 1);
        deletechar(m_csiesc.arg[0]);
        break;
    case 'Z': // CBT -- Cursor Backward Tabulation <n> tab stops
        DEFAULT(m_csiesc.arg[0], 1);
        puttab(-m_csiesc.arg[0]);
        break;
    case 'd': // VPA -- Move to <row>
        DEFAULT(m_csiesc.arg[0], 1);
        moveato(m_cursor.col, m_csiesc.arg[0]-1);
        break;
    case 'h': // SM -- Set terminal mode
        settmode(m_csiesc.priv, true, m_csiesc.arg, m_csiesc.narg);
        break;
    case 'm': // SGR -- Terminal attribute (color)
        setattr(m_csiesc.arg, m_csiesc.narg);
        break;
    case 'n': // DSR – Device Status Report (cursor position)
        if (m_csiesc.arg[0] == 6)
        {
            std::string seq = fmt::format(
                    "\033[{};{}R",
                    m_cursor.row+1, m_cursor.col+1);
            g_tty->write(seq.c_str(), seq.size());
        }
        break;
    case 'r': // DECSTBM -- Set Scrolling Region
        if (m_csiesc.priv)
            goto unknown;
        else
        {
            DEFAULT(m_csiesc.arg[0], 1);
            DEFAULT(m_csiesc.arg[1], m_rows);
            setscroll(m_csiesc.arg[0]-1, m_csiesc.arg[1]-1);
            moveato(0, 0);
        }
        break;
    case 's': // DECSC -- Save cursor position (ANSI.SYS)
        cursor(CURSOR_SAVE);
        break;
    case 'u': // DECRC -- Restore cursor position (ANSI.SYS)
        cursor(CURSOR_LOAD);
        break;
    case ' ':
        switch (m_csiesc.mode[1])
        {
        case 'q': // DECSCUSR -- Set Cursor Style
            DEFAULT(m_csiesc.arg[0], 1);
            switch(m_csiesc.arg[0])
            {
            case 2: // Steady Block
                m_cursortype = CURSOR_STEADY_BLOCK;
                break;
            case 3: // Blinking Underline
                m_cursortype = CURSOR_BLINK_UNDER;
                start_blink();
                break;
            case 4: // Steady Underline
                m_cursortype = CURSOR_STEADY_UNDER;
                break;
            case 5: // Blinking bar
                m_cursortype = CURSOR_BLINK_BAR;
                start_blink();
                break;
            case 6: // Steady bar
                m_cursortype = CURSOR_STEADY_BAR;
                break;
            case 0: // Blinking Block
            case 1: // Blinking Block (Default)
            default:
                m_cursortype = CURSOR_BLINK_BLOCK;
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
    fmt::MemoryWriter msg;
    msg << "ESC[";

    for (int i = 0; i < m_csiesc.len; i++)
    {
        unsigned int c = m_csiesc.buf[i] & 0xff;
        if (isprint(c))
            msg << (char) c;
        else if (c == '\n')
            msg << "(\\n)";
        else if (c == '\r')
            msg << "(\\r)";
        else if (c == 0x1b)
            msg << "(\\e)";
        else
            msg.write("(0x{:02X})", c);
    }

    return msg.str();
}

void TermImpl::setattr(int *attr, int len)
{
    for (int i = 0; i < len; i++)
    {
        switch (attr[i])
        {
        case 0:
            m_cursor.attr.attr.reset(ATTR_BOLD);
            m_cursor.attr.attr.reset(ATTR_FAINT);
            m_cursor.attr.attr.reset(ATTR_ITALIC);
            m_cursor.attr.attr.reset(ATTR_UNDERLINE);
            m_cursor.attr.attr.reset(ATTR_BLINK);
            m_cursor.attr.attr.reset(ATTR_REVERSE);
            m_cursor.attr.attr.reset(ATTR_INVISIBLE);
            m_cursor.attr.attr.reset(ATTR_STRUCK);
            m_cursor.attr.fg = m_deffg;
            m_cursor.attr.bg = m_defbg;
            break;
        case 1:
            m_cursor.attr.attr.set(ATTR_BOLD);
            break;
        case 2:
            m_cursor.attr.attr.set(ATTR_FAINT);
            break;
        case 3:
            m_cursor.attr.attr.set(ATTR_ITALIC);
            break;
        case 4:
            m_cursor.attr.attr.set(ATTR_UNDERLINE);
            break;
        case 5: // slow blink
        case 6: // rapid blink
            m_cursor.attr.attr.set(ATTR_BLINK);
            break;
        case 7:
            m_cursor.attr.attr.set(ATTR_REVERSE);
            break;
        case 8:
            m_cursor.attr.attr.set(ATTR_INVISIBLE);
            break;
        case 9:
            m_cursor.attr.attr.set(ATTR_STRUCK);
            break;
        case 22:
            m_cursor.attr.attr.reset(ATTR_BOLD);
            m_cursor.attr.attr.reset(ATTR_FAINT);
            break;
        case 23:
            m_cursor.attr.attr.reset(ATTR_ITALIC);
            break;
        case 24:
            m_cursor.attr.attr.reset(ATTR_UNDERLINE);
            break;
        case 25:
            m_cursor.attr.attr.reset(ATTR_BLINK);
            break;
        case 27:
            m_cursor.attr.attr.reset(ATTR_REVERSE);
            break;
        case 28:
            m_cursor.attr.attr.reset(ATTR_INVISIBLE);
            break;
        case 29:
            m_cursor.attr.attr.reset(ATTR_STRUCK);
            break;
        case 38:
            // todo: remove temp dump
            LOGGER()->debug("change fg: {}", strdump());
            /*
             todo: color
            if ((int idx = tdefcolor(attr, &i, l)) >= 0)
                m_cursor.attr.fg = idx;
                */
            break;
        case 39:
            m_cursor.attr.fg = m_deffg;
            break;
        case 48:
            // todo: remove temp dump
            LOGGER()->debug("change bg: {}", strdump());
            /*
             todo: color
            if ((int idx = tdefcolor(attr, &i, l)) >= 0)
                m_cursor.attr.bg = idx;
                */
            break;
        case 49:
            m_cursor.attr.bg = m_defbg;
            break;
        default:
            if (30 <= attr[i] && attr[i] <= 37)
                m_cursor.attr.fg = attr[i] - 30;
            else if (40 <= attr[i] && attr[i] <= 47)
                m_cursor.attr.bg = attr[i] - 40;
            else if (90 <= attr[i] && attr[i] <= 97)
                m_cursor.attr.fg = attr[i] - 90 + 8;
            else if (100 <= attr[i] && attr[i] <= 107)
                m_cursor.attr.bg = attr[i] - 100 + 8;
            else
            {
                LOGGER()->error(
                        "erresc(default): gfx attr {} unknown, {}",
                        attr[i], csidump());
            }
            break;
        }
    }
}

void TermImpl::settmode(bool priv, bool set, int *args, int narg)
{
    int *lim;
    term_mode mode;
    int alt;

    for (lim = args + narg; args < lim; ++args)
    {
        if (priv)
        {
            switch (*args)
            {
            case 1: // DECCKM -- Cursor key
                m_mode.set(MODE_APPCURSOR, set);
                break;
            case 5: // DECSCNM -- Reverse video
                mode = m_mode;
                m_mode.set(MODE_REVERSE, set);
                if (mode != m_mode)
                    rwte.refresh();
                break;
            case 6: // DECOM -- Origin
                if (set)
                    m_cursor.state |= CURSOR_ORIGIN;
                else
                    m_cursor.state &= ~CURSOR_ORIGIN;

                moveato(0, 0);
                break;
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
            case 9:    // X10 mouse compatibility mode
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
                cursor(set ? CURSOR_SAVE : CURSOR_LOAD);
                // FALLTHROUGH
            case 47: // swap screen
            case 1047:
                if (!allow_alt_screen())
                    break;
                alt = m_mode[MODE_ALTSCREEN];
                if (alt)
                    clearregion(0, 0, m_cols-1, m_rows-1);
                if (set ^ alt)
                    swapscreen();
                if (*args != 1049)
                    break;
                // FALLTHROUGH
            case 1048:
                cursor(set ? CURSOR_SAVE : CURSOR_LOAD);
                break;
            case 2004: // 2004: bracketed paste mode
                m_mode.set(MODE_BRCKTPASTE, set);
                break;
            // unimplemented mouse modes:
            case 1001: // VT200 mouse highlight mode; can hang the terminal
            case 1005: // UTF-8 mouse mode; will confuse non-UTF-8 applications
            case 1015: // urxvt mangled mouse mode; incompatible
                       // and can be mistaken for other control codes
                LOGGER()->warn( "unsupported mouse mode requested {}", *args);
                break;
            default:
                LOGGER()->error(
                        "erresc: unknown private set/reset mode {}",
                        *args);
                break;
            }
        }
        else
        {
            switch (*args)
            {
            case 0:  // Error (IGNORED)
                break;
            case 2:  // KAM -- keyboard action
                m_mode.set(MODE_KBDLOCK, set);
                break;
            case 4:  // IRM -- Insertion-replacement
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

void TermImpl::getbuttoninfo(int col, int row, const keymod_state& mod)
{
    m_sel.alt = m_mode[MODE_ALTSCREEN];

    m_sel.oe.col = col;
    m_sel.oe.row = row;
    selnormalize();

#define X(m, t) \
    {if ((mod & m) == m) { m_sel.type = t; return; }}
    SEL_MASKS
#undef X

    m_sel.type = SEL_REGULAR;
}

char *TermImpl::getsel()
{
    char *str, *ptr;
    int row, bufsize, lastcol, llen;
    Glyph *gp, *last;

    if (m_sel.ob.col == -1)
        return nullptr;

    bufsize = (m_cols+1) * (m_sel.ne.row-m_sel.nb.row+1) * utf_size;
    ptr = str = new char[bufsize];

    // append every set & selected glyph to the selection
    for (row = m_sel.nb.row; row <= m_sel.ne.row; row++)
    {
        if ((llen = linelen(row)) == 0)
        {
            *ptr++ = '\n';
            continue;
        }

        if (m_sel.type == SEL_RECTANGULAR)
        {
            gp = &m_lines[row][m_sel.nb.col];
            lastcol = m_sel.ne.col;
        }
        else
        {
            gp = &m_lines[row][m_sel.nb.row == row ? m_sel.nb.col : 0];
            lastcol = (m_sel.ne.row == row) ? m_sel.ne.col : m_cols-1;
        }
        last = &m_lines[row][MIN(lastcol, llen-1)];
        while (last >= gp && last->u == ' ')
            --last;

        for ( ; gp <= last; ++gp)
        {
            if (gp->attr[ATTR_WDUMMY])
                continue;

            ptr += utf8encode(gp->u, ptr);
        }

        // use \n for line ending in outgoing data
        if ((row < m_sel.ne.row || lastcol >= llen) && !(last->attr[ATTR_WRAP]))
            *ptr++ = '\n';
    }
    *ptr = 0;
    return str;
}

Term::Term(int cols, int rows) :
    impl(std::make_unique<TermImpl>(cols, rows))
{ }

Term::~Term()
{ }

const Glyph& Term::glyph(int col, int row) const
{ return impl->glyph(col, row); }

void Term::reset()
{ impl->reset(); }

void Term::resize(int cols, int rows)
{ impl->resize(cols, rows); }

void Term::setprint()
{ impl->setprint(); }

const term_mode& Term::mode() const
{ return impl->mode(); }

void Term::blink()
{ impl->blink(); }

const Selection& Term::sel() const
{ return impl->sel(); }

const Cursor& Term::cursor() const
{ return impl->cursor(); }

cursor_type Term::cursortype() const
{ return impl->cursortype(); }

bool Term::isdirty(int row) const
{ return impl->isdirty(row); }

void Term::setdirty()
{ impl->setdirty(); }

void Term::cleardirty(int row)
{ impl->cleardirty(row); }

void Term::putc(Rune u)
{ impl->putc(u); }

void Term::mousereport(int col, int row, mouse_event_enum evt, int button,
        const keymod_state& mod)
{ impl->mousereport(col, row, evt, button, mod); }

int Term::rows() const
{ return impl->rows(); }

int Term::cols() const
{ return impl->cols(); }

uint32_t Term::deffg() const
{ return impl->deffg(); }

uint32_t Term::defbg() const
{ return impl->defbg(); }

uint32_t Term::defcs() const
{ return impl->defcs(); }

uint32_t Term::defrcs() const
{ return impl->defrcs(); }

void Term::setfocused(bool focused)
{ impl->setfocused(focused); }

bool Term::focused() const
{ return impl->focused(); }

void Term::selclear()
{ impl->selclear(); }

void Term::clipcopy()
{ impl->clipcopy(); }

void Term::send(const char *data, std::size_t len /* = 0 */)
{
    if (!len)
        len = std::strlen(data);
    impl->send(data, len);
}
