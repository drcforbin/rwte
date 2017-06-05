#include <cstdint>
#include <cstdio>
#include <unistd.h>
#include <limits.h>

#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <cairo/cairo-xcb.h>

// uses explicit as a field name. ugh.
#define explicit explt
#include <xcb/xkb.h>
#undef explicit

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-x11.h>

#include "rwte/config.h"
#include "rwte/renderer.h"
#include "rwte/logging.h"
#include "rwte/rwte.h"
#include "rwte/term.h"
#include "rwte/tty.h"
#include "rwte/utf8.h"
#include "rwte/window.h"
#include "rwte/luastate.h"
#include "rwte/luaterm.h"

#define LOGGER() (logging::get("window"))

#define MAX(a, b) ((a) < (b)? (b) : (a))

// XEMBED messages
#define XEMBED_FOCUS_IN  4
#define XEMBED_FOCUS_OUT 5

static const keymod_state EMPTY_MASK; // no mods
static const keymod_state SHIFT_MASK(1 << MOD_SHIFT);
static const keymod_state ALT_MASK(1 << MOD_ALT);
static const keymod_state CTRL_MASK(1 << MOD_CTRL);
static const keymod_state LOGO_MASK(1 << MOD_LOGO);

static int get_border_px()
{
    // if invalid, default to 2
    auto L = rwte.lua();
    L->getglobal("config");
    L->getfield(-1, "border_px");
    int border_px = L->tointegerdef(-1, 2);
    L->pop(2);

    return border_px;
}

// main structure for window data
class WindowImpl
{
public:
    WindowImpl();
    ~WindowImpl();

    bool create(int cols, int rows);
    void destroy();

    void resize(uint16_t width, uint16_t height);

    uint32_t windowid() const { return win; }
    uint16_t width() const { return m_width; }
    uint16_t height() const { return m_height; }
    uint16_t rows() const { return m_rows; }
    uint16_t cols() const { return m_cols; }
    uint16_t tw() const { return m_tw; }
    uint16_t th() const { return m_th; }

    void draw();
    void set_wm_class();
    void settitle(const std::string& name);
    void seturgent(bool urgent);
    void bell(int volume);

    void setsel(char *sel);
    void selpaste();
    void setclip(char *sel);
    void clippaste();

private:
    void set_wmmachine_name();

    void drawregion(int x1, int y1, int x2, int y2);
    void drawglyph(const Glyph& glyph, int row, int col);

    bool load_keymap();

    void handle_key_press(ev::loop_ref&, xcb_key_press_event_t *event);
    void handle_client_message(ev::loop_ref& loop, xcb_client_message_event_t *event);
    void handle_motion_notify(ev::loop_ref&, xcb_motion_notify_event_t *event);
    void handle_visibility_notify(ev::loop_ref&, xcb_visibility_notify_event_t *event);
    void handle_unmap_notify(ev::loop_ref&, xcb_unmap_notify_event_t *event);
    void handle_focus_in(ev::loop_ref&, xcb_focus_in_event_t *event);
    void handle_focus_out(ev::loop_ref&, xcb_focus_out_event_t *event);
    void handle_button(ev::loop_ref&, xcb_button_press_event_t *event);
    void handle_selection_clear(ev::loop_ref&, xcb_selection_clear_event_t *event);
    void handle_selection_notify(ev::loop_ref&, xcb_selection_notify_event_t *event);
    void handle_property_notify(ev::loop_ref&, xcb_property_notify_event_t *event);
    void handle_selection_request(ev::loop_ref&, xcb_selection_request_event_t *event);
    void handle_map_notify(ev::loop_ref&, xcb_map_notify_event_t *event);
    void handle_expose(ev::loop_ref&, xcb_expose_event_t *event);
    void handle_configure_notify(ev::loop_ref&, xcb_configure_notify_event_t *event);
    void handle_xkb_event(xcb_generic_event_t *gevent);

    void selnotify(xcb_atom_t property, bool propnotify);

    // helper to call the handlers with their expected args
    template<typename evt_type> void call_handler(
            void (WindowImpl::*handler)(ev::loop_ref&, evt_type *),
            ev::loop_ref& loop,
            xcb_generic_event_t *event)
    {
        (this->*handler)(loop, reinterpret_cast<evt_type*>(event));
    }

    void handle_xcb_event(ev::loop_ref& loop, int type, xcb_generic_event_t *event);

    void readcb(ev::io &, int);
    void preparecb(ev::prepare &, int);
    void checkcb(ev::check &, int);

    xcb_connection_t *connection;
    xcb_drawable_t win;

    bool visible;
    bool mapped;
    bool focused;

    xcb_atom_t wmprotocols;
    xcb_atom_t wmdeletewin;

    xcb_atom_t xembed;
    xcb_atom_t xtarget;

    uint8_t xkb_base_event;

    struct xkb_state *xkb_state;
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_compose_table *xkb_compose_table;
    struct xkb_compose_state *xkb_compose_state;

    void register_atoms();
    void setup_xkb();
    bool load_compose_table(const char *locale);

    uint16_t m_tw, m_th;
    uint16_t m_width, m_height;
    uint16_t m_rows, m_cols;

    int m_scrno;
    xcb_screen_t *m_screen;
    xcb_visualtype_t *m_visual_type;

    xcb_atom_t m_netwmname;
    xcb_atom_t m_netwmpid;
    xcb_atom_t m_clipboard;
    xcb_atom_t m_incr;
    xcb_atom_t m_xseldata;
    xcb_atom_t m_targets;

    ev::io m_io;
    ev::prepare m_prepare;
    ev::check m_check;

    std::unique_ptr<Renderer> m_renderer;

    keymod_state m_keymod;
    xkb_mod_index_t m_shift_modidx, m_ctrl_modidx, m_alt_modidx, m_logo_modidx;
    char *m_primarysel;
    char *m_clipboardsel;
    uint32_t m_eventmask;
};

WindowImpl::WindowImpl() :
    m_primarysel(nullptr),
    m_clipboardsel(nullptr),
    m_eventmask(0)
{
    // this io watcher is just to to kick the loop around
    // when there is data available to be read
    m_io.set<WindowImpl,&WindowImpl::readcb>(this);
    m_prepare.set<WindowImpl,&WindowImpl::preparecb>(this);
    m_check.set<WindowImpl,&WindowImpl::checkcb>(this);
}

WindowImpl::~WindowImpl()
{
    if (!m_primarysel)
        delete[] m_primarysel;
    if (!m_clipboardsel)
        delete[] m_clipboardsel;
}

bool WindowImpl::create(int cols, int rows)
{
    connection = xcb_connect(NULL, &m_scrno);
    if (xcb_connection_has_error(connection))
        LOGGER()->fatal("Could not connect to X11 server");

    m_screen = xcb_aux_get_screen(connection, m_scrno);
    if (!m_screen)
        LOGGER()->fatal("Could not get default screen");

    m_visual_type = xcb_aux_get_visualtype(connection,
            m_scrno, m_screen->root_visual);
    if (!m_visual_type) {
        LOGGER()->error("could not get default screen visual type");
        return false;
    }

    m_renderer = std::make_unique<Renderer>();

    // arbitrary width and height
    auto root_surface = cairo_xcb_surface_create(connection,
            m_screen->root, m_visual_type, 20, 20);
    m_renderer->load_font(root_surface);
    cairo_surface_destroy(root_surface);

    int border_px = get_border_px();
    int width = cols * m_renderer->charwidth() + 2 * border_px;
    int height = rows * m_renderer->charheight() + 2 * border_px;
    m_width = width;
    m_height = height;

    // Create the window
    win = xcb_generate_id(connection);

    m_eventmask =
        XCB_EVENT_MASK_EXPOSURE |
        XCB_EVENT_MASK_STRUCTURE_NOTIFY |
        XCB_EVENT_MASK_KEY_PRESS |
        XCB_EVENT_MASK_POINTER_MOTION |
        XCB_EVENT_MASK_BUTTON_PRESS |
        XCB_EVENT_MASK_BUTTON_RELEASE |
        XCB_EVENT_MASK_VISIBILITY_CHANGE |
        XCB_EVENT_MASK_FOCUS_CHANGE;

    const uint32_t mask = XCB_CW_EVENT_MASK;
    uint32_t values[1] = { m_eventmask };

    auto cookie = xcb_create_window_checked(connection,  // Connection
            XCB_COPY_FROM_PARENT,          // depth (same as root)
            win,                    // window Id
            m_screen->root,           // parent window
            0, 0,                          // x, y
            width, height,   // width, height
            0,                             // border_width
            XCB_WINDOW_CLASS_INPUT_OUTPUT, // class
            m_screen->root_visual,    // visual
            mask, // mask
            values );   // mask

    xcb_generic_error_t *err;
    if ((err = xcb_request_check(connection, cookie)) != NULL)
    {
        LOGGER()->error("could not create window, code: {}", err->error_code);
        return false;
    }

    register_atoms();

    set_wm_class();

    // set WM_PROTOCOLS to WM_DELETE_WINDOW
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, win,
            wmprotocols, XCB_ATOM_ATOM, 32, 1, &wmdeletewin);

    if (xtarget == XCB_ATOM_NONE)
        xtarget = XCB_ATOM_STRING;

    settitle(options.title);

    pid_t pid = getpid();
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, win,
            m_netwmpid, XCB_ATOM_CARDINAL, 32, 1, &pid);
    set_wmmachine_name();

    setup_xkb();

    // map the window on the screen and flush
    xcb_map_window(connection, win);
    xcb_flush(connection);

    // start our event watchers
    m_prepare.start();
    m_check.start();
    m_io.start(xcb_get_file_descriptor(connection), ev::READ);

    return true;
}

void WindowImpl::destroy()
{
    m_renderer.reset();
    xcb_disconnect(connection);
}

void WindowImpl::resize(uint16_t width, uint16_t height)
{
    m_width = width;
    m_height = height;

    uint16_t cw = m_renderer->charwidth();
    uint16_t ch = m_renderer->charheight();

    int border_px = get_border_px();

    m_cols = (width - 2 * border_px) / cw;
    m_rows = (height - 2 * border_px) / ch;

    m_tw = MAX(1, m_cols * cw);
    m_th = MAX(1, m_rows * ch);

    m_renderer->resize(width, height);
    LOGGER()->info("resize to {}x{}", width, height);
}

void WindowImpl::draw()
{
    if (!visible)
        return;

    m_renderer->drawregion(0, 0, g_term->rows(), g_term->cols());
}

static std::string get_term_name()
{
    auto L = rwte.lua();
    L->getglobal("config");
    L->getfield(-1, "term_name");
    const char * s = L->tostring(-1);
    if (!s)
        LOGGER()->fatal("config.term_name is not valid");
    std::string name = s;
    L->pop(2);
    return name;
}

void WindowImpl::set_wm_class()
{
    std::string c;
    std::string term_name;

    if (!options.winname.empty())
        c = options.winname;
    else
    {
        // use termname if unspecified
        term_name = get_term_name();
        c = term_name;
    }

    // add the 0 that separates the parts
    c.push_back(0);

    if (!options.winclass.empty())
        c += options.winclass;
    else
    {
        // use termname if unspecified
        if (term_name.empty())
            term_name = get_term_name();
        c += term_name;
    }

    // set WM_CLASS (including the last null!)
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, win,
            XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, c.size()+1, c.c_str());
}

void WindowImpl::settitle(const std::string& name)
{
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, win,
            XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, name.size(), name.c_str());
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, win,
            m_netwmname, XCB_ATOM_STRING, 8, name.size(), name.c_str());
}

void WindowImpl::seturgent(bool urgent)
{
    // todo
}

void WindowImpl::bell(int volume)
{
    // todo
}

void WindowImpl::setsel(char *sel)
{
    if (m_primarysel)
        delete[] m_primarysel;
    m_primarysel = sel;

    xcb_set_selection_owner(connection, win, XCB_ATOM_PRIMARY, XCB_CURRENT_TIME);
    xcb_get_selection_owner_reply_t *reply = xcb_get_selection_owner_reply(connection,
                xcb_get_selection_owner(connection, XCB_ATOM_PRIMARY), NULL);
    if (reply)
    {
        if (reply->owner != win)
            g_term->selclear();

        std::free(reply);
    }
    else
        LOGGER()->error("unable to become clipboard owner!");
}

void WindowImpl::selpaste()
{
    // request primary sel as utf8 to m_xseldata
    xcb_convert_selection(connection, win, XCB_ATOM_PRIMARY,
            xtarget, m_xseldata, XCB_CURRENT_TIME);
}

void WindowImpl::setclip(char *sel)
{
    if (m_clipboardsel)
        delete[] m_clipboardsel;
    m_clipboardsel = sel;

    xcb_set_selection_owner(connection, win, m_clipboard, XCB_CURRENT_TIME);
    xcb_get_selection_owner_reply_t *reply = xcb_get_selection_owner_reply(connection,
                xcb_get_selection_owner(connection, m_clipboard), NULL);
    if (reply)
    {
        if (reply->owner != win)
            g_term->selclear();

        std::free(reply);
    }
    else
        LOGGER()->error("unable to become clipboard owner!");
}

void WindowImpl::clippaste()
{
    // request primary sel as utf8 to m_xseldata
    xcb_convert_selection(connection, win, m_clipboard,
            xtarget, m_xseldata, XCB_CURRENT_TIME);
}

void WindowImpl::set_wmmachine_name()
{
    char hostname[HOST_NAME_MAX + 1];
    if (gethostname(hostname, HOST_NAME_MAX + 1) == 0)
    {
        hostname[HOST_NAME_MAX] = '\0'; // safety first!
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, win,
                XCB_ATOM_WM_CLIENT_MACHINE, XCB_ATOM_STRING,
                8, strlen(hostname), hostname);
    }
}

void WindowImpl::register_atoms()
{
    const char * atom_names[] = {
        "WM_PROTOCOLS",
        "WM_DELETE_WINDOW",
        "_XEMBED",
        "_NET_WM_NAME",
        "_NET_WM_PID",
        "UTF8_STRING",
        "CLIPBOARD",
        "INCR",
        "XSEL_DATA",
        "TARGETS"
    };
    const int num_atoms = std::extent<decltype(atom_names)>::value;

    xcb_intern_atom_cookie_t cookies[num_atoms];
    for ( int i = 0; i < num_atoms; i++ )
        cookies[i] = xcb_intern_atom(connection,
                0, strlen(atom_names[i]), atom_names[i]);

    xcb_atom_t atoms[num_atoms];
    for ( int i = 0; i < num_atoms; i++ )
    {
        auto reply = xcb_intern_atom_reply(
                connection, cookies[i], 0);
        if (reply)
        {
            atoms[i] = reply->atom;
            free(reply);
        }
        else
            LOGGER()->error("unable to intern {}", atom_names[i]);
    }

    // keep some atoms around
    wmprotocols = atoms[0];
    wmdeletewin = atoms[1];
    xembed = atoms[2];
    m_netwmname = atoms[3];
    m_netwmpid = atoms[4];
    xtarget = atoms[5];
    m_clipboard = atoms[6];
    m_incr = atoms[7];
    m_xseldata = atoms[8];
    m_targets = atoms[9];
}

void WindowImpl::setup_xkb()
{
    static uint8_t xkb_base_error; // todo: work this out in the event loop
    if (xkb_x11_setup_xkb_extension(connection,
            XKB_X11_MIN_MAJOR_XKB_VERSION,
            XKB_X11_MIN_MINOR_XKB_VERSION,
            XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
            NULL,
            NULL,
            &xkb_base_event,
            &xkb_base_error) != 1)
        LOGGER()->fatal("could not setup XKB extension.");

    const uint16_t required_map_parts =
        (XCB_XKB_MAP_PART_KEY_TYPES |
         XCB_XKB_MAP_PART_KEY_SYMS |
         XCB_XKB_MAP_PART_MODIFIER_MAP |
         XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS |
         XCB_XKB_MAP_PART_KEY_ACTIONS |
         XCB_XKB_MAP_PART_VIRTUAL_MODS |
         XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP);

    const uint16_t required_events =
        (XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY |
         XCB_XKB_EVENT_TYPE_MAP_NOTIFY |
         XCB_XKB_EVENT_TYPE_STATE_NOTIFY);

    xcb_xkb_select_events(
        connection,
        xkb_x11_get_core_keyboard_device_id(connection),
        required_events,
        0,
        required_events,
        required_map_parts,
        required_map_parts,
        0);

    /// load initial keymap or exit
    if (!load_keymap())
        LOGGER()->fatal("could not load keymap");

    const char *locale = getenv("LC_ALL");
    if (!locale)
        locale = getenv("LC_CTYPE");
    if (!locale)
        locale = getenv("LANG");
    if (!locale) {
        LOGGER()->debug("unable to detect locale, fallback to C");
        locale = "C";
    }

    load_compose_table(locale);
}

// Loads the XKB keymap from the X11 server and feeds it to xkbcommon.
// Necessary so that we can properly let xkbcommon track the keyboard state and
// translate keypresses to utf-8.
bool WindowImpl::load_keymap()
{
    if (xkb_context == NULL)
    {
        if ((xkb_context = xkb_context_new((xkb_context_flags) 0)) == NULL)
        {
            LOGGER()->error("could not create xkbcommon context");
            return false;
        }
    }

    xkb_keymap_unref(xkb_keymap);

    int32_t device_id = xkb_x11_get_core_keyboard_device_id(connection);
    LOGGER()->debug("device = {}", device_id);
    if ((xkb_keymap = xkb_x11_keymap_new_from_device(xkb_context, connection,
            device_id, (xkb_keymap_compile_flags) 0)) == NULL)
    {
        LOGGER()->error("xkb_x11_keymap_new_from_device failed");
        return false;
    }

    struct xkb_state *new_state =
        xkb_x11_state_new_from_device(xkb_keymap, connection, device_id);
    if (new_state == NULL) {
        LOGGER()->error("xkb_x11_state_new_from_device failed");
        return false;
    }

    xkb_state_unref(xkb_state);
    xkb_state = new_state;

    m_shift_modidx = xkb_keymap_mod_get_index(xkb_keymap, XKB_MOD_NAME_SHIFT);
    m_ctrl_modidx = xkb_keymap_mod_get_index(xkb_keymap, XKB_MOD_NAME_CTRL);
    m_alt_modidx = xkb_keymap_mod_get_index(xkb_keymap, XKB_MOD_NAME_ALT);
    m_logo_modidx = xkb_keymap_mod_get_index(xkb_keymap, XKB_MOD_NAME_LOGO);

    return true;
}

// loads the XKB compose table from the given locale.
bool WindowImpl::load_compose_table(const char *locale)
{
    xkb_compose_table_unref(xkb_compose_table);

    if ((xkb_compose_table = xkb_compose_table_new_from_locale(xkb_context,
            locale, (xkb_compose_compile_flags) 0)) == NULL)
    {
        LOGGER()->error("xkb_compose_table_new_from_locale failed");
        return false;
    }

    struct xkb_compose_state *new_compose_state = xkb_compose_state_new(
            xkb_compose_table, (xkb_compose_state_flags) 0);
    if (new_compose_state == NULL)
    {
        LOGGER()->error("xkb_compose_state_new failed");
        return false;
    }

    xkb_compose_state_unref(xkb_compose_state);
    xkb_compose_state = new_compose_state;

    return true;
}

// todo: refactor this!
static void inittty()
{
    if (!g_tty)
    {
        g_tty = std::make_unique<Tty>();
        g_tty->resize();
    }
}

void WindowImpl::handle_key_press(ev::loop_ref&, xcb_key_press_event_t *event)
{
    auto& mode = g_term->mode();
    if (mode[MODE_KBDLOCK])
    {
        LOGGER()->info("key press while locked {}", event->detail);
        return;
    }

    xkb_keysym_t ksym = xkb_state_key_get_one_sym(xkb_state, event->detail);

    // The buffer will be null-terminated, so n >= 2 for 1 actual character.
    char buffer[128];
    memset(buffer, 0, sizeof(buffer));

    int len = 0;
    bool composed = false;
    if (xkb_compose_state && xkb_compose_state_feed(xkb_compose_state, ksym) == XKB_COMPOSE_FEED_ACCEPTED)
    {
        switch (xkb_compose_state_get_status(xkb_compose_state))
        {
            case XKB_COMPOSE_NOTHING:
                break;
            case XKB_COMPOSE_COMPOSING:
                return;
            case XKB_COMPOSE_COMPOSED:
                len = xkb_compose_state_get_utf8(xkb_compose_state, buffer, sizeof(buffer));
                ksym = xkb_compose_state_get_one_sym(xkb_compose_state);
                composed = true;
                break;
            case XKB_COMPOSE_CANCELLED:
                xkb_compose_state_reset(xkb_compose_state);
                return;
        }
    }

    if (!composed)
        len = xkb_state_key_get_utf8(xkb_state, event->detail, buffer, sizeof(buffer));

    //LOGGER()->debug("ksym {:x}, composed {}, '{}' ({})", ksym, composed, buffer, len);

    // todo: move arrow keys
    switch (ksym)
    {
        case XKB_KEY_Left:
        case XKB_KEY_Up:
        case XKB_KEY_Right:
        case XKB_KEY_Down:
            buffer[0] = '\033';

            if (m_keymod[MOD_SHIFT] || m_keymod[MOD_CTRL])
            {
                if (!m_keymod[MOD_CTRL])
                    buffer[1] = '[';
                else
                    buffer[1] = 'O';

                buffer[2] = "dacb"[ksym-XKB_KEY_Left];
            }
            else
            {
                if (!mode[MODE_APPCURSOR])
                    buffer[1] = '[';
                else
                    buffer[1] = 'O';

                buffer[2] = "DACB"[ksym-XKB_KEY_Left];
            }

            buffer[3] = 0;
            g_term->send(buffer);
            return;
    }

    auto L = rwte.lua();
    if (luaterm_key_press(L.get(), ksym, m_keymod))
        return;

    if (len == 1 && m_keymod[MOD_ALT])
    {
        if (mode[MODE_8BIT])
        {
            if (*buffer < 0177) {
                Rune c = *buffer | 0x80;
                len = utf8encode(c, buffer);
            }
        }
        else
        {
            buffer[1] = buffer[0];
            buffer[0] = '\033';
            len = 2;
        }
    }

    g_tty->write(buffer, len);
}

void WindowImpl::handle_client_message(ev::loop_ref& loop, xcb_client_message_event_t *event)
{
    LOGGER()->debug("handle_client_message type={} data={}",
            event->type, event->data.data32[0]);

    /*
    // See xembed specs
    // http://standards.freedesktop.org/xembed-spec/xembed-spec-latest.html
    if (event->type == xembed && event->format == 32)
    {
        if (event->data.data[1] == XEMBED_FOCUS_IN)
        {
            LOGGER()->info("xembed focus_in");
            // todo: something on focus_in
            //focused = true;
            //xseturgency(0);
        }
        else if (event->data.data[1] == XEMBED_FOCUS_OUT)
        {
            LOGGER()->info("xembed focus_in");
            // todo: something on focus_out
            //focused = false;
        }
    }
    else*/
    if (event->type == wmprotocols &&
            event->data.data32[0] == wmdeletewin)
    {
        LOGGER()->debug("wm_delete_window");

        // hang up on the shell and get out of here
        g_tty->hup();
        loop.break_loop(ev::ALL);
    }
}

void WindowImpl::handle_motion_notify(ev::loop_ref&, xcb_motion_notify_event_t *event)
{
    int col = m_renderer->x2col(event->event_x);
    int row = m_renderer->y2row(event->event_y);
    g_term->mousereport(col, row, MOUSE_MOTION, 0, m_keymod);
}

void WindowImpl::handle_visibility_notify(ev::loop_ref&, xcb_visibility_notify_event_t *event)
{
    visible = event->state != XCB_VISIBILITY_FULLY_OBSCURED;
}

void WindowImpl::handle_unmap_notify(ev::loop_ref&, xcb_unmap_notify_event_t *event)
{
    mapped = false;
}

void WindowImpl::handle_focus_in(ev::loop_ref&, xcb_focus_in_event_t *event)
{
    seturgent(false);
    g_term->setfocused(true);
}

void WindowImpl::handle_focus_out(ev::loop_ref&, xcb_focus_out_event_t *event)
{
    g_term->setfocused(false);
}

void WindowImpl::handle_button(ev::loop_ref&, xcb_button_press_event_t *event)
{
    int button = event->detail;
    bool press = (event->response_type & 0x7F) == XCB_BUTTON_PRESS;

    int col = m_renderer->x2col(event->event_x);
    int row = m_renderer->y2row(event->event_y);
    mouse_event_enum mouse_evt = press? MOUSE_PRESS : MOUSE_RELEASE;
    g_term->mousereport(col, row, mouse_evt, button, m_keymod);
}

void WindowImpl::handle_selection_clear(ev::loop_ref&, xcb_selection_clear_event_t *event)
{
    g_term->selclear();
}

void WindowImpl::handle_selection_notify(ev::loop_ref&, xcb_selection_notify_event_t *event)
{
    LOGGER()->info("handle_selection_notify: requestor={} selection={} target={} property={}",
            event->requestor, event->selection, event->target, event->property);

    selnotify(event->property, false);
}

void WindowImpl::handle_property_notify(ev::loop_ref&, xcb_property_notify_event_t *event)
{
    if (LOGGER()->level() <= logging::debug)
    {
        const char *atom_name;
        if (event->atom == XCB_ATOM_PRIMARY)
            atom_name = "PRIMARY";
        else if (event->atom == m_clipboard)
            atom_name = "clipboard";
        else if (event->atom == m_xseldata)
            atom_name = "xseldata";
        else
            atom_name = "unknown";

        LOGGER()->info("handle_property_notify: state={} atom={} ({})",
                event->state, event->atom, atom_name);
    }

    if (event->state == XCB_PROPERTY_NEW_VALUE &&
            (event->atom == XCB_ATOM_PRIMARY || event->atom == m_clipboard))
    {
        LOGGER()->debug("got clipboard new value");
        selnotify(event->atom, true);
    }
}

void WindowImpl::handle_selection_request(ev::loop_ref&, xcb_selection_request_event_t *event)
{
    xcb_atom_t property = XCB_NONE; // default to reject

    // old clients don't set property; set it to target
    if (event->property == XCB_NONE)
        event->property = event->target;

    if (event->target == m_targets)
    {
        LOGGER()->debug("requested targets");

        // respond with the supported type
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, event->requestor,
                event->property, XCB_ATOM_ATOM, 32, 1, &xtarget);
        property = event->property;
    }
    else if (event->target == xtarget || event->target == XCB_ATOM_STRING)
    {
        if (LOGGER()->level() <= logging::debug)
        {
            const char *target_type;
            if (event->target == xtarget)
                target_type = "utf8";
            else
                target_type = "other";

            const char *selection_name;
            if (event->selection == XCB_ATOM_PRIMARY)
                selection_name = "PRIMARY";
            else if (event->selection == m_clipboard)
                selection_name = "clipboard";
            else
                selection_name = "unknown";

            LOGGER()->debug("requested {} string to {}",
                    target_type, selection_name);
        }

        // with XCB_ATOM_STRING non ascii characters may be incorrect in the
        // requestor. not our problem, use utf8
        char *seltext = nullptr;
        if (event->selection == XCB_ATOM_PRIMARY)
            seltext = m_primarysel;
        else if (event->selection == m_clipboard)
            seltext = m_clipboardsel;
        else
        {
            LOGGER()->error("unhandled selection {:#x}", event->selection);
            return;
        }

        if (seltext)
        {
            if (win == event->requestor)
            {
                if (event->property == m_xseldata)
                    LOGGER()->debug("setting self xseldata",
                            event->property);
                else
                    LOGGER()->debug("setting self property={}",
                            event->property);
            }
            else
                LOGGER()->debug("setting {} property={}",
                        event->requestor, event->property);

            xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
                    event->requestor, event->property, event->target,
                    8, strlen(seltext), seltext);
            property = event->property;
        }
    }

    // X11 events are all 32 bytes
    uint8_t *buffer = new uint8_t[32];
    memset(buffer, 0, 32);

    auto ev = reinterpret_cast<xcb_selection_notify_event_t *>(buffer);
    ev->response_type = XCB_SELECTION_NOTIFY;
    ev->time = event->time;
    ev->requestor = event->requestor;
    ev->selection = event->selection;
    ev->target = event->target;
    ev->property = property;

    if (win == event->requestor)
        LOGGER()->debug("sending self selection={} target={} property={}",
                ev->selection, ev->target, ev->property);
    else
        LOGGER()->debug("sending requestor={} selection={} target={} property={}",
                ev->requestor, ev->selection, ev->target, ev->property);

    xcb_send_event(connection, false, event->requestor,
                XCB_EVENT_MASK_NO_EVENT, (const char*) buffer);
    delete[] buffer;
}

void WindowImpl::handle_map_notify(ev::loop_ref&, xcb_map_notify_event_t *event)
{
    LOGGER()->info("handle_map_notify");
    mapped = true;

    // get the initial mapped size
    xcb_get_geometry_reply_t *geo = xcb_get_geometry_reply(connection,
                xcb_get_geometry(connection, win), NULL);
    if (geo)
    {
        int width = geo->width;
        int height = geo->height;
        std::free(geo);

        // renderer owns this surface
        auto surface = cairo_xcb_surface_create(connection,
                win, m_visual_type, width, height);
        m_renderer->set_surface(surface, width, height);

        rwte.resize(width, height);

        inittty();
    }
    else
        LOGGER()->error("unable to determine geometry!");
}

void WindowImpl::handle_expose(ev::loop_ref&, xcb_expose_event_t *event)
{
    // redraw only on the last expose event in the sequence
    if (event->count == 0 && mapped && visible)
    {
        g_term->setdirty();
        draw();
    }
}

void WindowImpl::handle_configure_notify(ev::loop_ref&, xcb_configure_notify_event_t *event)
{
    if (mapped)
        rwte.resize(event->width, event->height);
}

// xkb event handler
void WindowImpl::handle_xkb_event(xcb_generic_event_t *gevent)
{
    union xkb_event {
        struct {
            uint8_t response_type;
            uint8_t xkbType;
            uint16_t sequence;
            xcb_timestamp_t time;
            uint8_t deviceID;
        } any;
        xcb_xkb_new_keyboard_notify_event_t new_keyboard_notify;
        xcb_xkb_map_notify_event_t map_notify;
        xcb_xkb_state_notify_event_t state_notify;
    } *event = (union xkb_event *)gevent;

    uint8_t core_device = xkb_x11_get_core_keyboard_device_id(connection);
    if (event->any.deviceID != core_device)
        return;

    // new keyboard notify + map notify capture all kinds of keymap
    // updates. state notify captures modifiers
    switch (event->any.xkbType)
    {
        case XCB_XKB_NEW_KEYBOARD_NOTIFY:
            if (event->new_keyboard_notify.changed & XCB_XKB_NKN_DETAIL_KEYCODES)
                load_keymap();
            break;
        case XCB_XKB_MAP_NOTIFY:
            load_keymap();
            break;
        case XCB_XKB_STATE_NOTIFY:
            xkb_state_update_mask(xkb_state,
                                  event->state_notify.baseMods,
                                  event->state_notify.latchedMods,
                                  event->state_notify.lockedMods,
                                  event->state_notify.baseGroup,
                                  event->state_notify.latchedGroup,
                                  event->state_notify.lockedGroup);

            m_keymod.reset();
            if (xkb_state_mod_index_is_active(xkb_state, m_shift_modidx,
                        XKB_STATE_MODS_EFFECTIVE) == 1)
                m_keymod.set(MOD_SHIFT);
            if (xkb_state_mod_index_is_active(xkb_state, m_alt_modidx,
                        XKB_STATE_MODS_EFFECTIVE) == 1)
                m_keymod.set(MOD_ALT);
            if (xkb_state_mod_index_is_active(xkb_state, m_ctrl_modidx,
                        XKB_STATE_MODS_EFFECTIVE) == 1)
                m_keymod.set(MOD_CTRL);
            if (xkb_state_mod_index_is_active(xkb_state, m_logo_modidx,
                        XKB_STATE_MODS_EFFECTIVE) == 1)
                m_keymod.set(MOD_LOGO);

            break;
        default:
            LOGGER()->debug("unhandled xkb event for device {} (core {}): {}",
                    event->any.deviceID, core_device, event->any.xkbType);
    }
}

void WindowImpl::handle_xcb_event(ev::loop_ref& loop, int type, xcb_generic_event_t *event)
{
    switch (type)
    {
        #define MESSAGE(msg, handler) \
            case msg: call_handler(&WindowImpl::handler, loop, event); break

        MESSAGE(XCB_KEY_PRESS, handle_key_press);
        MESSAGE(XCB_BUTTON_PRESS, handle_button);
        MESSAGE(XCB_BUTTON_RELEASE, handle_button);
        MESSAGE(XCB_UNMAP_NOTIFY, handle_unmap_notify);
        MESSAGE(XCB_EXPOSE, handle_expose);
        MESSAGE(XCB_MOTION_NOTIFY, handle_motion_notify);
        MESSAGE(XCB_CLIENT_MESSAGE, handle_client_message);
        MESSAGE(XCB_FOCUS_IN, handle_focus_in);
        MESSAGE(XCB_FOCUS_OUT, handle_focus_out);
        MESSAGE(XCB_PROPERTY_NOTIFY, handle_property_notify);
        MESSAGE(XCB_CONFIGURE_NOTIFY, handle_configure_notify);
        MESSAGE(XCB_VISIBILITY_NOTIFY, handle_visibility_notify);
        MESSAGE(XCB_SELECTION_CLEAR, handle_selection_clear);
        MESSAGE(XCB_SELECTION_NOTIFY, handle_selection_notify);
        MESSAGE(XCB_SELECTION_REQUEST, handle_selection_request);
        MESSAGE(XCB_MAP_NOTIFY, handle_map_notify);

        #undef MESSAGE

        // known, ignored
        case XCB_REPARENT_NOTIFY: // 21
        case XCB_KEY_RELEASE: // 3
        case XCB_MAP_REQUEST: // 20
        case XCB_DESTROY_NOTIFY: // 17
        case XCB_ENTER_NOTIFY: // 7
        case XCB_CONFIGURE_REQUEST: // 23
        case XCB_MAPPING_NOTIFY: // 34
            break;
        default:
            if (type == xkb_base_event)
                handle_xkb_event(event);
            else
                LOGGER()->warn("unknown message: {}", type);
    }
}

void WindowImpl::selnotify(xcb_atom_t property, bool propnotify)
{
    if (property == XCB_ATOM_NONE)
    {
        LOGGER()->debug("got no data");
        return;
    }

    xcb_get_property_reply_t *reply = xcb_get_property_reply(connection,
            xcb_get_property(connection, 0, win, property, XCB_GET_PROPERTY_TYPE_ANY, 0, UINT_MAX), NULL);
    if (reply)
    {
        int len = xcb_get_property_value_length(reply);
        if (propnotify && len == 0)
        {
            // propnotify with no data means all data has been
            // transferred, and we no longer need property
            // notify events
            m_eventmask &= ~XCB_EVENT_MASK_PROPERTY_CHANGE;

            const uint32_t mask = XCB_CW_EVENT_MASK;
            uint32_t values[1] = {
                m_eventmask
            };
            xcb_change_window_attributes(connection, win, mask, values);
        }

        if (reply->type == m_incr)
        {
            // activate property change events so we receive
            // notification about the next chunk
            m_eventmask |= XCB_EVENT_MASK_PROPERTY_CHANGE;

            const uint32_t mask = XCB_CW_EVENT_MASK;
            uint32_t values[1] = { m_eventmask };
            xcb_change_window_attributes(connection, win, mask, values);

            // deleting the property is transfer start signal
            xcb_delete_property(connection, win, property);
        }
        else if (len > 0)
        {
            char *data = (char *) xcb_get_property_value(reply);

            // fix line endings (\n -> \r)
            char *repl = data;
            char *last = data + len;
            while ((repl = (char *) memchr(repl, '\n', last - repl)))
                *repl++ = '\r';

            // todo: move to a Term::paste function
            bool brcktpaste = g_term->mode()[MODE_BRCKTPASTE];
            if (brcktpaste)
                g_tty->write("\033[200~", 6);
            g_tty->write(data, len);
            if (brcktpaste)
                g_tty->write("\033[201~", 6);
        }

        free(reply);
    }
    else
        LOGGER()->error("unable to get clip property!");
}

// this callback is a noop, work is done by the prepare and check
void WindowImpl::readcb(ev::io &, int)
{ }

// flush before blocking (and waiting for new events)
void WindowImpl::preparecb(ev::prepare &, int)
{
    xcb_flush(connection);
}

// after handling other events, call xcb_poll_for_event
void WindowImpl::checkcb(ev::check &w, int)
{
    xcb_generic_event_t *event;

    while ((event = xcb_poll_for_event(connection)) != NULL)
    {
        if (event->response_type == 0)
        {
            //if (event_is_ignored(event->sequence, 0))
            //    LOGGER()->debug("expected X11 Error received for sequence {:#x}", event->sequence);
            //else {
                auto error = reinterpret_cast<xcb_generic_error_t *>(event);
                LOGGER()->error("X11 Error received! sequence {:#x}, error_code = {}",
                     error->sequence, error->error_code);
            //}
        }
        else
        {
            // clear high bit (indicates generated)
            int type = (event->response_type & 0x7F);
            handle_xcb_event(w.loop, type, event);
        }

        free(event);
    }
}

Window::Window() :
    impl(std::make_unique<WindowImpl>())
{ }

Window::~Window()
{ }

bool Window::create(int cols, int rows)
{ return impl->create(cols, rows); }

void Window::destroy()
{ impl->destroy(); }

void Window::resize(uint16_t width, uint16_t height)
{ impl->resize(width, height); }

uint32_t Window::windowid() const
{ return impl->windowid(); }

uint16_t Window::width() const
{ return impl->width(); }

uint16_t Window::height() const
{ return impl->height(); }

uint16_t Window::rows() const
{ return impl->rows(); }

uint16_t Window::cols() const
{ return impl->cols(); }

uint16_t Window::tw() const
{ return impl->tw(); }

uint16_t Window::th() const
{ return impl->th(); }

void Window::draw()
{ impl->draw(); }

void Window::settitle(const std::string& name)
{ impl->settitle(name); }

void Window::seturgent(bool urgent)
{ impl->seturgent(urgent); }

void Window::bell(int volume)
{ impl->bell(volume); }

void Window::setsel(char *sel)
{ impl->setsel(sel); }

void Window::selpaste()
{ impl->selpaste(); }

void Window::setclip(char *sel)
{ impl->setclip(sel); }

void Window::clippaste()
{ impl->clippaste(); }
