#include "lua/config.h"
#include "lua/state.h"
#include "lua/window.h"
#include "rw/logging.h"
#include "rwte/config.h"
#include "rwte/coords.h"
#include "rwte/renderer.h"
#include "rwte/rwte.h"
#include "rwte/selection.h"
#include "rwte/term.h"
#include "rwte/tty.h"
#include "rwte/window-internal.h"
#include "rwte/window.h"

#include <cairo/cairo-xcb.h>
#include <cstdint>
#include <cstdio>
#include <limits.h>
#include <string_view>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>

using namespace std::literals;

// xkb uses explicit as a field name. ugh.
// clang complains about redefining explicit,
// so we need to tell it to shut up, but GCC
// don't care
#if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wkeyword-macro"
#endif

#define explicit _explicit
#include <xcb/xkb.h>
#undef explicit

#if defined(__clang__)
#    pragma clang diagnostic pop
#endif

#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon.h>

#define LOGGER() (rw::logging::get("xcbwindow"))

// XEMBED messages
// todo: static const / constexpr
#define XEMBED_FOCUS_IN 4
#define XEMBED_FOCUS_OUT 5

/// @file
/// @brief Implements a xcb based window

namespace xcbwin {

// todo: move this to a utils file
static int get_border_px()
{
    // if invalid, default to 2
    return lua::config::get_int("border_px", 2);
}

// main structure for window data
class XcbWindow final : public Window
{
public:
    XcbWindow(std::shared_ptr<event::Bus> bus, std::shared_ptr<term::Term> term,
            std::shared_ptr<Tty> tty);
    ~XcbWindow();

    uint32_t windowid() const { return win; }

    int fd() const;
    void prepare();
    bool event();
    bool check();

    uint16_t width() const { return m_width; }
    uint16_t height() const { return m_height; }
    uint16_t rows() const { return m_rows; }
    uint16_t cols() const { return m_cols; }

    void draw();
    void settitle(std::string_view name);
    void seturgent(bool urgent);
    void bell(int volume);

    void setsel();
    void selpaste();
    void setclip();
    void clippaste();

private:
    void settitlecore(std::string_view name);
    void set_wm_class();
    void set_wmmachine_name();

    void drawregion(int x1, int y1, int x2, int y2);

    bool load_keymap();

    void publishresize(uint16_t width, uint16_t height);
    void onresize(const event::Resize& evt);

    bool handle_key_press(xcb_key_press_event_t* event);
    bool handle_client_message(xcb_client_message_event_t* event);
    bool handle_motion_notify(xcb_motion_notify_event_t* event);
    bool handle_visibility_notify(xcb_visibility_notify_event_t* event);
    bool handle_unmap_notify(xcb_unmap_notify_event_t* event);
    bool handle_focus_in(xcb_focus_in_event_t* event);
    bool handle_focus_out(xcb_focus_out_event_t* event);
    bool handle_button(xcb_button_press_event_t* event);
    bool handle_selection_clear(xcb_selection_clear_event_t* event);
    bool handle_selection_notify(xcb_selection_notify_event_t* event);
    bool handle_property_notify(xcb_property_notify_event_t* event);
    bool handle_selection_request(xcb_selection_request_event_t* event);
    bool handle_map_notify(xcb_map_notify_event_t* event);
    bool handle_expose(xcb_expose_event_t* event);
    bool handle_configure_notify(xcb_configure_notify_event_t* event);
    bool handle_xkb_event(xcb_generic_event_t* gevent);

    void selnotify(xcb_atom_t property, bool propnotify);

    // helper to call the handlers with their expected args
    template <typename evt_type>
    bool call_handler(
            bool (XcbWindow::*handler)(evt_type*),
            xcb_generic_event_t* event)
    {
        return (this->*handler)(reinterpret_cast<evt_type*>(event));
    }

    bool handle_xcb_event(int type, xcb_generic_event_t* event);

    xcb_connection_t* connection = nullptr;
    xcb_drawable_t win = 0;

    bool visible = false;
    bool mapped = false;
    bool focused = false;

    xcb_atom_t m_wmprotocols;
    xcb_atom_t m_wmdeletewin;

    xcb_atom_t m_xembed;
    xcb_atom_t m_xtarget;

    uint8_t xkb_base_event;

    struct xkb_state* xkb_state = nullptr;
    struct xkb_context* xkb_context = nullptr;
    struct xkb_keymap* xkb_keymap = nullptr;
    struct xkb_compose_table* xkb_compose_table = nullptr;
    struct xkb_compose_state* xkb_compose_state = nullptr;

    void register_atoms();
    void setup_xkb();
    void load_compose_table(const char* locale);

    std::shared_ptr<event::Bus> m_bus;
    std::shared_ptr<term::Term> m_term;
    std::shared_ptr<Tty> m_tty;

    int m_resizeReg;

    uint16_t m_width, m_height;
    uint16_t m_rows, m_cols;

    int m_scrno;
    xcb_screen_t* m_screen;
    xcb_visualtype_t* m_visual_type;

    xcb_atom_t m_netwmname;
    xcb_atom_t m_netwmpid;
    xcb_atom_t m_clipboard;
    xcb_atom_t m_incr;
    xcb_atom_t m_xseldata;
    xcb_atom_t m_targets;

    std::unique_ptr<renderer::Renderer> m_renderer;

    term::keymod_state m_keymod;
    xkb_mod_index_t m_shift_modidx, m_ctrl_modidx, m_alt_modidx, m_logo_modidx;
    uint32_t m_eventmask;
};

XcbWindow::XcbWindow(std::shared_ptr<event::Bus> bus,
        std::shared_ptr<term::Term> term, std::shared_ptr<Tty> tty) :
    m_bus(std::move(bus)),
    m_term(std::move(term)),
    m_tty(std::move(tty)),
    m_resizeReg(m_bus->reg<event::Resize, XcbWindow, &XcbWindow::onresize>(this)),
    m_eventmask(0)
{
    int cols = m_term->cols();
    int rows = m_term->rows();

    connection = xcb_connect(nullptr, &m_scrno);
    if (xcb_connection_has_error(connection))
        throw WindowError("could not connect to X11 server");

    m_screen = xcb_aux_get_screen(connection, m_scrno);
    if (!m_screen)
        throw WindowError("could not get default screen");

    m_visual_type = xcb_aux_get_visualtype(connection,
            m_scrno, m_screen->root_visual);
    if (!m_visual_type)
        throw WindowError("could not get default screen visual type");

    m_renderer = std::make_unique<renderer::Renderer>(m_term.get());

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

    constexpr uint32_t mask = XCB_CW_EVENT_MASK;
    const uint32_t values[1] = {m_eventmask};

    auto cookie = xcb_create_window_checked(connection, // Connection
            XCB_COPY_FROM_PARENT,                       // depth (same as root)
            win,                                        // window Id
            m_screen->root,                             // parent window
            0, 0,                                       // x, y
            width, height,                              // width, height
            0,                                          // border_width
            XCB_WINDOW_CLASS_INPUT_OUTPUT,              // class
            m_screen->root_visual,                      // visual
            mask,                                       // mask
            values);                                    // mask

    xcb_generic_error_t* err;
    if ((err = xcb_request_check(connection, cookie)) != nullptr) {
        throw WindowError(fmt::format(
                "could not create window, code: {}", err->error_code));
    }

    register_atoms();

    set_wm_class();

    // set WM_PROTOCOLS to WM_DELETE_WINDOW
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, win,
            m_wmprotocols, XCB_ATOM_ATOM, 32, 1, &m_wmdeletewin);

    if (m_xtarget == XCB_ATOM_NONE)
        m_xtarget = XCB_ATOM_STRING;

    settitlecore(options.title);

    pid_t pid = getpid();
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, win,
            m_netwmpid, XCB_ATOM_CARDINAL, 32, 1, &pid);
    set_wmmachine_name();

    setup_xkb();

    // map the window on the screen and flush
    xcb_map_window(connection, win);
    xcb_flush(connection);
}

XcbWindow::~XcbWindow()
{
    m_renderer.reset();
    xcb_disconnect(connection);

    m_bus->unreg<event::Resize>(m_resizeReg);
}

int XcbWindow::fd() const
{
    return xcb_get_file_descriptor(connection);
}

void XcbWindow::prepare()
{
    // flush before blocking (and waiting for new events)
    xcb_flush(connection);
}

bool XcbWindow::event()
{
    // this callback is a noop, work is done by the prepare and check
    // this is really just to to kick the loop around when there is data
    // available to be read

    // todo: do we really not want to do the 'check' work here?

    // don't stop
    return false;
}

bool XcbWindow::check()
{
    // after handling other events, call xcb_poll_for_event
    xcb_generic_event_t* event;

    bool stop = false;
    while (!stop && (event = xcb_poll_for_event(connection)) != nullptr) {
        if (event->response_type == 0) {
            //if (event_is_ignored(event->sequence, 0))
            //    LOGGER()->debug("expected X11 Error received for sequence {:#x}", event->sequence);
            //else {
            auto error = reinterpret_cast<xcb_generic_error_t*>(event);
            LOGGER()->error("X11 Error received! sequence {:#x}, error_code = {}",
                    error->sequence, error->error_code);
            //}
        } else {
            // clear high bit (indicates generated)
            int type = (event->response_type & 0x7F);
            stop = handle_xcb_event(type, event);
        }

        std::free(event);
    }

    return stop;
}

void XcbWindow::draw()
{
    if (visible) {
        m_renderer->drawregion({0, 0}, {m_term->rows(), m_term->cols()});
    }
}

static std::string get_term_name()
{
    auto name = lua::config::get_string("term_name");
    if (name.empty())
        LOGGER()->fatal("config.term_name is not valid");
    return name;
}

void XcbWindow::settitle(std::string_view name)
{
    settitlecore(name);
}

void XcbWindow::seturgent(bool urgent)
{
    // todo
}

void XcbWindow::bell(int volume)
{
    // todo
}

void XcbWindow::setsel()
{
    xcb_set_selection_owner(connection, win, XCB_ATOM_PRIMARY, XCB_CURRENT_TIME);
    xcb_get_selection_owner_reply_t* reply = xcb_get_selection_owner_reply(connection,
            xcb_get_selection_owner(connection, XCB_ATOM_PRIMARY), nullptr);
    if (reply) {
        if (reply->owner != win)
            m_term->selclear();

        std::free(reply);
    } else
        LOGGER()->error("unable to become clipboard owner!");
}

void XcbWindow::selpaste()
{
    // request primary sel as utf8 to m_xseldata
    xcb_convert_selection(connection, win, XCB_ATOM_PRIMARY,
            m_xtarget, m_xseldata, XCB_CURRENT_TIME);
}

void XcbWindow::setclip()
{
    xcb_set_selection_owner(connection, win, m_clipboard, XCB_CURRENT_TIME);
    xcb_get_selection_owner_reply_t* reply = xcb_get_selection_owner_reply(connection,
            xcb_get_selection_owner(connection, m_clipboard), nullptr);
    if (reply) {
        if (reply->owner != win)
            m_term->selclear();

        std::free(reply);
    } else
        LOGGER()->error("unable to become clipboard owner!");
}

void XcbWindow::clippaste()
{
    // request clipboard sel as utf8 to m_xseldata
    xcb_convert_selection(connection, win, m_clipboard,
            m_xtarget, m_xseldata, XCB_CURRENT_TIME);
}

void XcbWindow::settitlecore(std::string_view name)
{
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, win,
            XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, name.size(), name.data());
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, win,
            m_netwmname, XCB_ATOM_STRING, 8, name.size(), name.data());
}

void XcbWindow::set_wm_class()
{
    std::string c;
    std::string term_name;

    if (!options.winname.empty())
        c = options.winname;
    else {
        // use termname if unspecified
        term_name = get_term_name();
        c = term_name;
    }

    // add the 0 that separates the parts
    c.push_back(0);

    if (!options.winclass.empty())
        c += options.winclass;
    else {
        // use termname if unspecified
        if (term_name.empty())
            term_name = get_term_name();
        c += term_name;
    }

    // set WM_CLASS (including the last null!)
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, win,
            XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, c.size() + 1, c.c_str());
}

void XcbWindow::set_wmmachine_name()
{
    char hostname[HOST_NAME_MAX + 1];
    if (gethostname(hostname, HOST_NAME_MAX + 1) == 0) {
        hostname[HOST_NAME_MAX] = '\0'; // safety first!
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, win,
                XCB_ATOM_WM_CLIENT_MACHINE, XCB_ATOM_STRING,
                8, strlen(hostname), hostname);
    }
}

void XcbWindow::register_atoms()
{
    constexpr std::array atom_names = {
            "WM_PROTOCOLS"sv,
            "WM_DELETE_WINDOW"sv,
            "_XEMBED"sv,
            "_NET_WM_NAME"sv,
            "_NET_WM_PID"sv,
            "UTF8_STRING"sv,
            "CLIPBOARD"sv,
            "INCR"sv,
            "XSEL_DATA"sv,
            "TARGETS"sv};

    std::array<xcb_intern_atom_cookie_t, atom_names.size()> cookies;
    for (std::size_t i = 0; i < atom_names.size(); i++)
        cookies[i] = xcb_intern_atom(connection,
                0, atom_names[i].size(), atom_names[i].data());

    std::array<xcb_atom_t, atom_names.size()> atoms;
    for (std::size_t i = 0; i < atom_names.size(); i++) {
        auto reply = xcb_intern_atom_reply(
                connection, cookies[i], nullptr);
        if (reply) {
            atoms[i] = reply->atom;
            std::free(reply);
        } else
            LOGGER()->error("unable to intern {}", atom_names[i]);
    }

    // keep some atoms around
    m_wmprotocols = atoms[0];
    m_wmdeletewin = atoms[1];
    m_xembed = atoms[2];
    m_netwmname = atoms[3];
    m_netwmpid = atoms[4];
    m_xtarget = atoms[5];
    m_clipboard = atoms[6];
    m_incr = atoms[7];
    m_xseldata = atoms[8];
    m_targets = atoms[9];
}

void XcbWindow::setup_xkb()
{
    if (xkb_x11_setup_xkb_extension(connection,
                XKB_X11_MIN_MAJOR_XKB_VERSION,
                XKB_X11_MIN_MINOR_XKB_VERSION,
                XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
                nullptr,
                nullptr,
                &xkb_base_event,
                nullptr) != 1)
        LOGGER()->fatal("could not setup XKB extension.");

    constexpr uint16_t required_map_parts =
            (XCB_XKB_MAP_PART_KEY_TYPES |
                    XCB_XKB_MAP_PART_KEY_SYMS |
                    XCB_XKB_MAP_PART_MODIFIER_MAP |
                    XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS |
                    XCB_XKB_MAP_PART_KEY_ACTIONS |
                    XCB_XKB_MAP_PART_VIRTUAL_MODS |
                    XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP);

    constexpr uint16_t required_events =
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
            nullptr);

    /// load initial keymap or exit
    if (!load_keymap())
        LOGGER()->fatal("could not load keymap");

    const char* locale = std::getenv("LC_ALL");
    if (!locale)
        locale = std::getenv("LC_CTYPE");
    if (!locale)
        locale = std::getenv("LANG");
    if (!locale) {
        LOGGER()->debug("unable to detect locale, fallback to C");
        locale = "C";
    }

    load_compose_table(locale);
}

// Loads the XKB keymap from the X11 server and feeds it to xkbcommon.
// Necessary so that we can properly let xkbcommon track the keyboard state and
// translate keypresses to utf-8.
bool XcbWindow::load_keymap()
{
    if (xkb_context == nullptr) {
        if ((xkb_context = xkb_context_new((xkb_context_flags) 0)) == nullptr) {
            LOGGER()->error("could not create xkbcommon context");
            return false;
        }
    }

    if (xkb_keymap)
        xkb_keymap_unref(xkb_keymap);

    int32_t device_id = xkb_x11_get_core_keyboard_device_id(connection);
    LOGGER()->debug("device = {}", device_id);
    if ((xkb_keymap = xkb_x11_keymap_new_from_device(xkb_context, connection,
                 device_id, (xkb_keymap_compile_flags) 0)) == nullptr) {
        LOGGER()->error("xkb_x11_keymap_new_from_device failed");
        return false;
    }

    struct xkb_state* new_state =
            xkb_x11_state_new_from_device(xkb_keymap, connection, device_id);
    if (new_state == nullptr) {
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
void XcbWindow::load_compose_table(const char* locale)
{
    xkb_compose_table_unref(xkb_compose_table);

    // todo: cleanup xkb_compose_table
    if ((xkb_compose_table = xkb_compose_table_new_from_locale(xkb_context,
                 locale, (xkb_compose_compile_flags) 0)) == nullptr) {
        LOGGER()->error("xkb_compose_table_new_from_locale failed");
        return;
    }

    struct xkb_compose_state* new_compose_state = xkb_compose_state_new(
            xkb_compose_table, (xkb_compose_state_flags) 0);
    if (new_compose_state == nullptr)
        LOGGER()->error("xkb_compose_state_new failed");
    else {
        xkb_compose_state_unref(xkb_compose_state);
        xkb_compose_state = new_compose_state;
    }
}

void XcbWindow::publishresize(uint16_t width, uint16_t height)
{
    if (m_width == width && m_height == height)
        return;

    m_width = width;
    m_height = height;

    uint16_t cw = m_renderer->charwidth();
    uint16_t ch = m_renderer->charheight();

    int border_px = get_border_px();

    m_cols = (width - 2 * border_px) / cw;
    m_rows = (height - 2 * border_px) / ch;

    m_bus->publish(event::Resize{m_width, m_height, m_cols, m_rows});
}

void XcbWindow::onresize(const event::Resize& evt)
{
    m_renderer->resize(evt.width, evt.height);
    LOGGER()->info("resize to {}x{}", evt.width, evt.height);
}

bool XcbWindow::handle_key_press(xcb_key_press_event_t* event)
{
    process_key(event->detail, m_term.get(), m_tty.get(), xkb_state,
            xkb_compose_state, m_keymod);

    return false;
}

bool XcbWindow::handle_client_message(xcb_client_message_event_t* event)
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
    if (event->type == m_wmprotocols &&
            event->data.data32[0] == m_wmdeletewin) {
        LOGGER()->debug("wm_delete_window");

        // hang up on the shell and get out of here
        m_tty->hup();
        return true;
    }

    return false;
}

bool XcbWindow::handle_motion_notify(xcb_motion_notify_event_t* event)
{
    auto cell = m_renderer->pxtocell(event->event_x, event->event_y);
    m_term->mousereport(cell, term::MOUSE_MOTION, 0, m_keymod);

    return false;
}

bool XcbWindow::handle_visibility_notify(xcb_visibility_notify_event_t* event)
{
    visible = event->state != XCB_VISIBILITY_FULLY_OBSCURED;

    return false;
}

bool XcbWindow::handle_unmap_notify(xcb_unmap_notify_event_t* event)
{
    mapped = false;

    return false;
}

bool XcbWindow::handle_focus_in(xcb_focus_in_event_t* event)
{
    seturgent(false);
    m_term->setfocused(true);

    return false;
}

bool XcbWindow::handle_focus_out(xcb_focus_out_event_t* event)
{
    m_term->setfocused(false);

    return false;
}

bool XcbWindow::handle_button(xcb_button_press_event_t* event)
{
    int button = event->detail;
    bool press = (event->response_type & 0x7F) == XCB_BUTTON_PRESS;

    auto cell = m_renderer->pxtocell(event->event_x, event->event_y);
    term::mouse_event_enum mouse_evt =
            press ? term::MOUSE_PRESS : term::MOUSE_RELEASE;
    m_term->mousereport(cell, mouse_evt, button, m_keymod);

    return false;
}

bool XcbWindow::handle_selection_clear(xcb_selection_clear_event_t* event)
{
    m_term->selclear();

    return false;
}

bool XcbWindow::handle_selection_notify(xcb_selection_notify_event_t* event)
{
    LOGGER()->info("handle_selection_notify: requestor={} selection={} target={} property={}",
            event->requestor, event->selection, event->target, event->property);

    selnotify(event->property, false);

    return false;
}

bool XcbWindow::handle_property_notify(xcb_property_notify_event_t* event)
{
    if (LOGGER()->level() <= rw::logging::log_level::debug) {
        const char* atom_name;
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
            (event->atom == XCB_ATOM_PRIMARY || event->atom == m_clipboard)) {
        LOGGER()->debug("got clipboard new value");
        selnotify(event->atom, true);
    }

    return false;
}

bool XcbWindow::handle_selection_request(xcb_selection_request_event_t* event)
{
    xcb_atom_t property = XCB_NONE; // default to reject

    // old clients don't set property; set it to target
    if (event->property == XCB_NONE)
        event->property = event->target;

    if (event->target == m_targets) {
        LOGGER()->debug("requested targets");

        // respond with the supported type
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, event->requestor,
                event->property, XCB_ATOM_ATOM, 32, 1, &m_xtarget);
        property = event->property;
    } else if (event->target == m_xtarget || event->target == XCB_ATOM_STRING) {
        if (LOGGER()->level() <= rw::logging::log_level::debug) {
            const char* target_type;
            if (event->target == m_xtarget)
                target_type = "utf8";
            else
                target_type = "other";

            const char* selection_name;
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
        // todo: make stringview, get rid of later strlen
        std::shared_ptr<char> seltext;
        if (event->selection == XCB_ATOM_PRIMARY)
            seltext = m_term->sel().primary;
        else if (event->selection == m_clipboard)
            seltext = m_term->sel().clipboard;
        else {
            LOGGER()->error("unhandled selection {:#x}", event->selection);
            return false;
        }

        if (seltext) {
            if (win == event->requestor) {
                if (event->property == m_xseldata)
                    LOGGER()->debug("setting self xseldata",
                            event->property);
                else
                    LOGGER()->debug("setting self property={}",
                            event->property);
            } else
                LOGGER()->debug("setting {} property={}",
                        event->requestor, event->property);

            xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
                    event->requestor, event->property, event->target,
                    8, std::strlen(seltext.get()), seltext.get());
            property = event->property;
        }
    }

    // X11 events are all 32 bytes
    uint8_t buffer[32];
    memset(buffer, 0, 32);

    auto ev = reinterpret_cast<xcb_selection_notify_event_t*>(buffer);
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

    return false;
}

bool XcbWindow::handle_map_notify(xcb_map_notify_event_t* event)
{
    LOGGER()->info("handle_map_notify");
    mapped = true;

    // get the initial mapped size
    xcb_get_geometry_reply_t* geo = xcb_get_geometry_reply(connection,
            xcb_get_geometry(connection, win), nullptr);
    if (geo) {
        int width = geo->width;
        int height = geo->height;
        std::free(geo);

        // renderer owns this surface
        auto surface = cairo_xcb_surface_create(connection,
                win, m_visual_type, width, height);
        m_renderer->set_surface(surface, width, height);

        publishresize(width, height);
    } else
        LOGGER()->error("unable to determine geometry!");

    return false;
}

bool XcbWindow::handle_expose(xcb_expose_event_t* event)
{
    // redraw only on the last expose event in the sequence
    if (event->count == 0 && mapped && visible) {
        m_term->setdirty();
        draw();
    }

    return false;
}

bool XcbWindow::handle_configure_notify(xcb_configure_notify_event_t* event)
{
    if (mapped)
        publishresize(event->width, event->height);

    return false;
}

// xkb event handler
bool XcbWindow::handle_xkb_event(xcb_generic_event_t* gevent)
{
    union xkb_event {
        struct
        {
            uint8_t response_type;
            uint8_t xkbType;
            uint16_t sequence;
            xcb_timestamp_t time;
            uint8_t deviceID;
        } any;
        xcb_xkb_new_keyboard_notify_event_t new_keyboard_notify;
        xcb_xkb_map_notify_event_t map_notify;
        xcb_xkb_state_notify_event_t state_notify;
    }* event = (union xkb_event*) gevent;

    uint8_t core_device = xkb_x11_get_core_keyboard_device_id(connection);
    if (event->any.deviceID != core_device)
        return false;

    // new keyboard notify + map notify capture all kinds of keymap
    // updates. state notify captures modifiers
    switch (event->any.xkbType) {
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
                m_keymod.set(term::MOD_SHIFT);
            if (xkb_state_mod_index_is_active(xkb_state, m_alt_modidx,
                        XKB_STATE_MODS_EFFECTIVE) == 1)
                m_keymod.set(term::MOD_ALT);
            if (xkb_state_mod_index_is_active(xkb_state, m_ctrl_modidx,
                        XKB_STATE_MODS_EFFECTIVE) == 1)
                m_keymod.set(term::MOD_CTRL);
            if (xkb_state_mod_index_is_active(xkb_state, m_logo_modidx,
                        XKB_STATE_MODS_EFFECTIVE) == 1)
                m_keymod.set(term::MOD_LOGO);

            break;
        default:
            LOGGER()->debug("unhandled xkb event for device {} (core {}): {}",
                    event->any.deviceID, core_device, event->any.xkbType);
    }

    return false;
}

bool XcbWindow::handle_xcb_event(int type, xcb_generic_event_t* event)
{
    switch (type) {
#define MESSAGE(msg, handler) \
    case msg:                 \
        return call_handler(&XcbWindow::handler, event)

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
        case XCB_REPARENT_NOTIFY:   // 21
        case XCB_KEY_RELEASE:       // 3
        case XCB_MAP_REQUEST:       // 20
        case XCB_DESTROY_NOTIFY:    // 17
        case XCB_ENTER_NOTIFY:      // 7
        case XCB_CONFIGURE_REQUEST: // 23
        case XCB_MAPPING_NOTIFY:    // 34
            break;
        default:
            if (type == xkb_base_event)
                handle_xkb_event(event);
            else
                LOGGER()->warn("unknown message: {}", type);
    }

    return false;
}

void XcbWindow::selnotify(xcb_atom_t property, bool propnotify)
{
    if (property == XCB_ATOM_NONE) {
        LOGGER()->debug("got no data");
        return;
    }

    xcb_get_property_reply_t* reply = xcb_get_property_reply(connection,
            xcb_get_property(connection, 0, win, property, XCB_GET_PROPERTY_TYPE_ANY, 0, UINT_MAX), nullptr);
    if (reply) {
        std::size_t len = xcb_get_property_value_length(reply);
        if (propnotify && len == 0) {
            // propnotify with no data means all data has been
            // transferred, and we no longer need property
            // notify events
            m_eventmask &= ~XCB_EVENT_MASK_PROPERTY_CHANGE;

            constexpr uint32_t mask = XCB_CW_EVENT_MASK;
            const uint32_t values[1] = {m_eventmask};
            xcb_change_window_attributes(connection, win, mask, values);
        }

        if (reply->type == m_incr) {
            // activate property change events so we receive
            // notification about the next chunk
            m_eventmask |= XCB_EVENT_MASK_PROPERTY_CHANGE;

            constexpr uint32_t mask = XCB_CW_EVENT_MASK;
            const uint32_t values[1] = {m_eventmask};
            xcb_change_window_attributes(connection, win, mask, values);

            // deleting the property is transfer start signal
            xcb_delete_property(connection, win, property);
        } else if (len > 0) {
            char* data = (char*) xcb_get_property_value(reply);

            // fix line endings (\n -> \r)
            char* repl = data;
            char* last = data + len;
            while ((repl = (char*) memchr(repl, '\n', last - repl)))
                *repl++ = '\r';

            // todo: move to a Term::paste function
            bool brcktpaste = m_term->mode()[term::MODE_BRCKTPASTE];
            if (brcktpaste)
                m_tty->write({"\033[200~", 6});
            m_tty->write({data, len});
            if (brcktpaste)
                m_tty->write({"\033[201~", 6});
        }

        std::free(reply);
    } else
        LOGGER()->error("unable to get clip property!");
}

} // namespace xcbwin

/// Returns a Window implemented with xcb
/// \param global bus
/// \return Window object
/// \addtogroup Window
/// @{
std::unique_ptr<Window> createXcbWindow(std::shared_ptr<event::Bus> bus,
        std::shared_ptr<term::Term> term, std::shared_ptr<Tty> tty)
{
    return std::make_unique<xcbwin::XcbWindow>(std::move(bus),
            std::move(term), std::move(tty));
}
/// @}
