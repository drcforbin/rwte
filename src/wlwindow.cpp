#include "lua/config.h"
#include "lua/state.h"
#include "lua/window.h"
#include "rwte/bufferpool.h"
#include "rwte/config.h"
#include "rwte/coords.h"
#include "rwte/logging.h"
#include "rwte/renderer.h"
#include "rwte/rwte.h"
#include "rwte/selection.h"
#include "rwte/term.h"
#include "rwte/tty.h"
#include "rwte/wayland.h"
#include "rwte/window-internal.h"
#include "rwte/window.h"
#include "xdg-shell/xdg-shell-client-protocol.h"

#include <cairo/cairo.h>
#include <ev++.h>
#include <linux/input.h>
#include <sys/mman.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>

#define LOGGER() (logging::get("wlwindow"))

/// @file
/// @brief Implements a wayland based window

namespace wlwin {

// todo: suspicious statics
bool queued = false;
bool prepared = false;

// todo: move this to a utils file
static int get_border_px()
{
    // if invalid, default to 2
    return lua::config::get_int("border_px", 2);
}

class WlWindow;
class Seat;
class XdgWmBase;
class Surface;
class XdgSurface;
class XdgToplevel;

struct PointerFrame
{
    bool entered = false;
    int mousex = 0;
    int mousey = 0;
};

class XdgToplevel :
    public wayland::XdgToplevel<XdgToplevel>
{
public:
    XdgToplevel(WlWindow* window, xdg_toplevel* toplevel) :
        wayland::XdgToplevel<XdgToplevel>(toplevel),
        window(window)
    {}

protected:
    friend class wayland::XdgToplevel<XdgToplevel>;

    void handle_configure(int32_t width, int32_t height,
            wl_array* states);

    void handle_close()
    {
        // todo: do we need to stop the event loop?
    }

private:
    WlWindow* window;
};

class Pointer;
class Keyboard;
class Touch;

class Seat :
    public wayland::Seat<Seat, Pointer, Keyboard, Touch>
{
    using Base = wayland::Seat<Seat, Pointer, Keyboard, Touch>;

public:
    Seat(WlWindow* window, wl_seat* seat) :
        Base(seat),
        m_window(window)
    {}

    WlWindow* window() { return m_window; }

    // todo: getter/setter?
    term::keymod_state keymod;

private:
    WlWindow* m_window;
};

class Pointer : public wayland::Pointer<Pointer>
{
public:
    Pointer(Seat* seat, wl_pointer* pointer) :
        wayland::Pointer<Pointer>(pointer),
        seat(seat)
    {}

protected:
    friend class wayland::Pointer<Pointer>;

    void handle_enter(uint32_t serial, wl_surface* surface,
            wl_fixed_t sx, wl_fixed_t sy);

    void handle_leave(uint32_t serial, wl_surface* surface)
    {
        frame.entered = false;
    }

    void handle_motion(uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
    {
        frame.mousex = wl_fixed_to_int(sx);
        frame.mousey = wl_fixed_to_int(sy);
    }

    void handle_frame();

private:
    PointerFrame frame;
    Seat* seat;
};

class Keyboard : public wayland::Keyboard<Keyboard>
{
public:
    Keyboard(Seat* seat, wl_keyboard* keyboard) :
        wayland::Keyboard<Keyboard>(keyboard),
        seat(seat)
    {}

    ~Keyboard()
    {
        // todo: move state, keymap to seat?
        if (state) {
            xkb_state_unref(state);
        }
        if (keymap) {
            xkb_keymap_unref(keymap);
        }
    }

protected:
    friend class wayland::Keyboard<Keyboard>;

    void handle_keymap(uint32_t format, int fd, uint32_t size);
    void handle_enter(uint32_t serial, wl_surface* surface,
            wl_array* keys);
    void handle_leave(uint32_t serial, wl_surface* surface);
    void handle_key(uint32_t serial, uint32_t time, uint32_t key,
            uint32_t state);
    void handle_modifiers(uint32_t serial, uint32_t mods_depressed,
            uint32_t mods_latched, uint32_t mods_locked,
            uint32_t group);
    void handle_repeat_info(int32_t rate, int32_t delay);

private:
    void load_compose_table(const char* locale);

    Seat* seat;

    // todo: move to seat?
    xkb_keymap* keymap = nullptr;
    xkb_state* state = nullptr;
    xkb_compose_table* compose_table = nullptr;
    xkb_compose_state* compose_state = nullptr;
};

class Touch : public wayland::Touch<Touch>
{
public:
    Touch(Seat* seat, wl_touch* touch) :
        wayland::Touch<Touch>(touch)
    {}

    // no handlers...yet
};

class Surface : public wayland::Surface<Surface>
{
public:
    Surface(WlWindow* window, wl_surface* surface) :
        wayland::Surface<Surface>(surface),
        window(window)
    {}

protected:
    friend class wayland::Surface<Surface>;

    void handle_enter(wl_output* output);
    void handle_leave(wl_output* output);

private:
    WlWindow* window;
};

class XdgSurface : public wayland::XdgSurface<XdgSurface, WlWindow, XdgToplevel>
{
    using Base = wayland::XdgSurface<XdgSurface, WlWindow, XdgToplevel>;

public:
    XdgSurface(WlWindow* window, xdg_surface* surface) :
        Base(window, surface)
    {}

    // no handlers
};

class XdgWmBase : public wayland::XdgWmBase<XdgWmBase, WlWindow, Surface, XdgSurface>
{
    using Base = wayland::XdgWmBase<XdgWmBase, WlWindow, Surface, XdgSurface>;

public:
    XdgWmBase(WlWindow* window, xdg_wm_base* wmbase) :
        Base(window, wmbase)
    {}

    // no handlers
};

class Registry : public wayland::Registry<Registry>
{
    using Base = wayland::Registry<Registry>;

public:
    Registry(WlWindow* window, wl_registry* registry) :
        Base(registry),
        window(window)
    {}

protected:
    friend Base;

    void handle_global(uint32_t name, const char* interface, uint32_t version);

private:
    WlWindow* window;
};

// main structure for window data
class WlWindow : public Window
{
public:
    WlWindow(std::shared_ptr<event::Bus> bus,
            std::shared_ptr<term::Term> term, std::shared_ptr<Tty> tty);
    ~WlWindow();

    // todo: does this make sense?
    uint32_t windowid() const { return 0; }

    uint16_t width() const { return m_width; }
    uint16_t height() const { return m_height; }
    uint16_t rows() const { return m_rows; }
    uint16_t cols() const { return m_cols; }

    // todo: make private
    void drawCore();

    void draw();
    void settitle(std::string_view name);

    // todo
    void seturgent(bool urgent) {}
    void bell(int volume) {}
    void setsel() {}
    void selpaste() {}
    void setclip() {}
    void clippaste() {}

    // called by funcs in here:

    void setpointer(const PointerFrame& frame);
    void setkbdfocus(bool focus);
    void publishresize(uint16_t width, uint16_t height);

    // todo: wrap these with accessor funcs?

    // todo: rename?
    xkb_context* ctx;
    // todo: compositor wrapper?
    wl_compositor* compositor = nullptr;
    std::unique_ptr<XdgWmBase> wmbase;
    std::unique_ptr<XdgToplevel> toplevel;
    std::unique_ptr<Seat> seat;
    std::unique_ptr<BufferPool> buffers;

    wl_cursor_theme* cursor_theme = nullptr;
    wl_cursor* default_cursor = nullptr;
    wl_surface* cursor_surface = nullptr;

    bool fullscreen = false;
    bool activated = false;
    bool visible = false;

    std::shared_ptr<event::Bus> m_bus;
    std::shared_ptr<term::Term> m_term;
    std::shared_ptr<Tty> m_tty;

private:
    void settitlecore(std::string_view name);
    void onresize(const event::Resize& evt);

    void iocb(ev::io&, int);
    void preparecb(ev::prepare&, int);

    void paint_pixels(Buffer* buffer);

    int m_resizeReg;

    uint16_t m_width, m_height;
    uint16_t m_rows, m_cols;

    ev::prepare m_prepare;
    ev::io m_io;

    std::unique_ptr<renderer::Renderer> m_renderer;

    // todo: make unique ptr? private?
    wl_display* display = nullptr;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<XdgSurface> xdg_surface;

    PointerFrame currPointer;
    bool kbdfocus;
};

WlWindow::WlWindow(std::shared_ptr<event::Bus> bus,
        std::shared_ptr<term::Term> term, std::shared_ptr<Tty> tty) :
    m_bus(std::move(bus)),
    m_term(std::move(term)),
    m_tty(std::move(tty)),
    m_resizeReg(m_bus->reg<event::Resize, WlWindow, &WlWindow::onresize>(this))
{
    m_prepare.set<WlWindow, &WlWindow::preparecb>(this);
    m_io.set<WlWindow, &WlWindow::iocb>(this);

    int cols = m_term->cols();
    int rows = m_term->rows();

    // todo: listen to display for errors
    // todo: better error handling here...
    display = wl_display_connect(nullptr);
    if (display == nullptr)
        throw WindowError("can't connect to display");
    LOGGER()->debug("connected to display");

    m_renderer = std::make_unique<renderer::Renderer>(m_term.get());

    {
        // arbitrary width and height
        auto surface = cairo_image_surface_create(
                CAIRO_FORMAT_ARGB32, 20, 20);
        m_renderer->load_font(surface);
        cairo_surface_destroy(surface);

        int border_px = get_border_px();
        int width = cols * m_renderer->charwidth() + 2 * border_px;
        int height = rows * m_renderer->charheight() + 2 * border_px;
        m_width = width;
        m_height = height;
        LOGGER()->debug("initial window size {}x{}, char size {}x{}",
                width, height,
                m_renderer->charwidth(), m_renderer->charheight());
    }

    ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx)
        throw WindowError("couldn't create xkb context");

    // todo: combine registry with display?
    Registry r(this, wl_display_get_registry(display));
    // todo: error handling
    wl_display_roundtrip(display);

    // todo: more checking
    if (compositor == nullptr || !wmbase)
        throw WindowError("can't find compositor or wm_base");
    LOGGER()->debug("found compositor");

    surface = std::make_unique<Surface>(this,
            wl_compositor_create_surface(compositor));
    xdg_surface = wmbase->get_xdg_surface(
            surface.get());
    toplevel = xdg_surface->get_xdg_toplevel();
    surface->commit();

    // todo: error handling
    wl_display_roundtrip(display);

    // todo: set app id
    // toplevel->set_app_id(?);

    // set initial title
    settitlecore(options.title);

    // todo: move this somewhere
    cursor_surface = wl_compositor_create_surface(compositor);

    if (!buffers->create_buffers(m_width, m_height))
        throw WindowError("unable to create shared buffers");
    LOGGER()->debug("created buffers");

    // initial draw to get the window mapped
    auto buffer = buffers->get_buffer();
    if (buffer) {
        // todo: initial fill of buffer with bg color?

        surface->attach(buffer->get(), 0, 0);
        surface->damage_buffer(0, 0, m_width, m_height);
        surface->commit();
    } else {
        LOGGER()->warn("unable to get a draw buffer");
    }

    // start our event watchers
    m_prepare.start();
    m_io.start(wl_display_get_fd(display), ev::READ);
    LOGGER()->debug("listening for display events");
}

WlWindow::~WlWindow()
{
    LOGGER()->debug("destroying window");

    // todo: make sure these are ordered in the class
    // so they can be destroyed by dtor without segfault

    // ordered cleanup...destroy toplevel, xdg_surface,
    // surface, then buffers
    toplevel.reset();
    xdg_surface.reset();
    surface.reset();

    // buffers and seat whenever
    buffers.reset();
    seat.reset();

    if (cursor_surface)
        wl_surface_destroy(cursor_surface);
    if (cursor_theme)
        wl_cursor_theme_destroy(cursor_theme);

    m_renderer.reset();

    wl_display_disconnect(display);
    LOGGER()->debug("disconnected from display");

    m_bus->unreg<event::Resize>(m_resizeReg);
}

void WlWindow::drawCore()
{
    LOGGER()->trace("draw {}x{}", m_width, m_height);

    auto buffer = buffers->get_buffer();
    if (buffer) {
        paint_pixels(buffer);

        surface->attach(buffer->get(), 0, 0);
        surface->damage_buffer(0, 0, m_width, m_height);
        surface->commit();
    } else {
        LOGGER()->warn("unable to get a draw buffer");
    }
}

// todo: constexpr?
static const wl_callback_listener frame_listener = {
        // done event
        [](void* data, wl_callback* cb, uint32_t time) {
            queued = false;
            LOGGER()->trace("frame received");
            static_cast<WlWindow*>(data)->drawCore();
        }};

void WlWindow::draw()
{
    if (!queued) {
        auto cb = wl_surface_frame(surface->get());
        wl_callback_add_listener(cb, &frame_listener, this);
        wl_surface_commit(surface->get());
        LOGGER()->trace("queued frame");
        queued = true;
    }
}

void WlWindow::settitle(std::string_view name)
{
    settitlecore(name);
}

void WlWindow::setpointer(const PointerFrame& frame)
{
    // todo: move to bus?

    if (frame.entered &&
            (currPointer.mousex != frame.mousex ||
                    currPointer.mousey != frame.mousey)) {
        auto cell = m_renderer->pxtocell(frame.mousex, frame.mousey);
        m_term->mousereport(cell, term::MOUSE_MOTION, 0, seat->keymod);
    }

    // todo: handle mouse button
    // auto cell = m_renderer->pxtocell(event->event_x, event->event_y);
    // mouse_event_enum mouse_evt = press? MOUSE_PRESS : MOUSE_RELEASE;
    // m_term->mousereport(cell, mouse_evt, button, seat->keymod);

    currPointer = frame;
}

// todo: remove?
void WlWindow::setkbdfocus(bool focus)
{
    if (kbdfocus != focus) {
        LOGGER()->trace("focused {} (keyboard)", focus);

        kbdfocus = focus;
        m_term->setfocused(focus);
    }
}

void WlWindow::publishresize(uint16_t width, uint16_t height)
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

    LOGGER()->debug("publishing resize to {}x{} / {}x{}",
            m_width, m_height, m_cols, m_rows);

    m_bus->publish(event::Resize{m_width, m_height, m_cols, m_rows});
}

void WlWindow::settitlecore(std::string_view name)
{
    LOGGER()->debug("setting title to {}", name);
    toplevel->set_title(name);
}

void WlWindow::onresize(const event::Resize& evt)
{
    LOGGER()->info("resize to {}x{}", evt.width, evt.height);

    // todo: render resize is done while painting, because
    // we have a hack in place that resizes every paint
    // (since we don't let it keep state), but doing it
    // here would be better
    // m_renderer->resize(evt.width, evt.height);

    // todo: figure out why we're painting badly on resize
    // (we paint with an unresized surface, then resize
    // and paint properly)

    buffers->resize(evt.width, evt.height);
}

void WlWindow::preparecb(ev::prepare&, int)
{
    // todo: test different kinds of failures in here

    if (!prepared) {
        // announce intention to read; if that fails, dispatch
        // anything already pending and repeat until successful
        LOGGER()->trace("preparecb prepare_read");
        while (wl_display_prepare_read(display) != 0)
            wl_display_dispatch_pending(display);

        // flush anything outgoing to the server
        LOGGER()->trace("preparecb flush");
        int ret = wl_display_flush(display);
        if (ret == -1) {
            if (errno == EAGAIN) {
                LOGGER()->debug("preparecb flush EAGAIN");
                // failed with eagain, start waiting for write
                // events and try again
                m_io.set(ev::READ | ev::WRITE);
            } else if (errno == EPIPE) {
                // removing this condition and logging will
                // make the following condition no longer
                // redundant
                LOGGER()->debug("preparecb flush error: {}", strerror(errno));
            } else if (errno != EPIPE) {
                LOGGER()->debug("preparecb flush error: {}", strerror(errno));
                // if we got some other than epipe, cancel the read
                // we signed up for earlier
                wl_display_cancel_read(display);
                m_io.set(ev::READ);
            }
        }

        LOGGER()->trace("preparecb prepare done");
        prepared = true;
    }
}

void WlWindow::iocb(ev::io&, int revents)
{
    // todo: test different kinds of failures in here

    if (revents & ev::WRITE) {
        LOGGER()->trace("iocb WRITE");

        // we're ready to write. try flushing again; if it
        // succeeds or fails with something other than eagain,
        // we can stop listening for write
        int ret = wl_display_flush(display);
        if (ret != -1 || errno != EAGAIN) {
            // if the error is EPIPE, we can just stop listening
            // for writes
            if (errno == EPIPE) {
                m_io.set(ev::READ);
            } else {
                // otherwise, we don't need to bother with the read
                wl_display_cancel_read(display);
                m_io.stop();

                // todo: log, exit?
            }
        }
    }

    if (revents & ev::READ) {
        // todo: how do we handle errors in polling?
        // if (has_error(ret))
        //     wl_display_cancel_read(display);

        // try reading and dispatching
        LOGGER()->trace("iocb read_events");
        if (wl_display_read_events(display) != -1) {
            LOGGER()->trace("iocb dispatch_pending");
            wl_display_dispatch_pending(display);
        } else if (errno && errno != EAGAIN) {
            LOGGER()->debug("iocb dispatch_pending error: {}", strerror(errno));
            // if we actually got an error on the read,
            // we don't need to keep reading
            m_io.stop();

            // todo: log, exit?
        } else if (errno) {
            LOGGER()->debug("iocb dispatch_pending EAGAIN");
        }

        LOGGER()->trace("iocb read done");
        prepared = false;
    }
}

void WlWindow::paint_pixels(Buffer* buffer)
{
    int width = buffer->width();
    int height = buffer->height();
    int stride = buffer->stride();

    // hack! renderer should keep this surface as state, or
    // we need to do something smarter about damaging the
    // buffer for painting...when we paint to multiple buffers
    // they aren't all fully painted
    auto surface = cairo_image_surface_create_for_data(buffer->data(),
            CAIRO_FORMAT_ARGB32, width, height, stride);
    m_renderer->set_surface(surface, width, height);
    // todo: this ok? used to be done in onresize?
    m_renderer->resize(width, height);
    m_renderer->drawregion({0, 0}, {m_rows, m_cols});
    m_renderer->set_surface(nullptr, width, height);
}

void XdgToplevel::handle_configure(int32_t width, int32_t height,
        wl_array* states)
{
    bool activated = false;
    bool fullscreen = false;

    // wl_array's data is a void*, and size is in bytes, but
    // it contains xdg_toplevel_state values
    for (auto cur = static_cast<xdg_toplevel_state*>(states->data);
            (const char*) cur < ((const char*) states->data + states->size);
            cur++) {
        if (*cur == XDG_TOPLEVEL_STATE_FULLSCREEN) {
            fullscreen = true;
        } else if (*cur == XDG_TOPLEVEL_STATE_ACTIVATED) {
            activated = true;
        }
    }

    // todo: remove? this doesn't drive behavior
    window->fullscreen = fullscreen;

    // todo: remove this too? also doesn't drive behavior
    if (window->activated != activated) {
        // docs say we shouldn't use this to assume the window actually
        // has keyboard or pointer focus, so lets do it instead in
        // keyboard enter/leave (this is just bookkeeping)
        window->activated = activated;
    }

    if (width != 0 && height != 0) {
        window->publishresize(width, height);
    }

    // todo: look at all the other m_term and m_tty uses in XcbWindow
}

void Pointer::handle_enter(uint32_t serial, wl_surface* surface,
        wl_fixed_t sx, wl_fixed_t sy)
{
    auto window = seat->window();

    frame.entered = true;
    frame.mousex = wl_fixed_to_int(sx);
    frame.mousey = wl_fixed_to_int(sy);

    // load and set the cursor
    auto image = window->default_cursor->images[0];
    auto buffer = wl_cursor_image_get_buffer(image);
    wl_pointer_set_cursor(pointer, serial,
            window->cursor_surface,
            image->hotspot_x,
            image->hotspot_y);
    wl_surface_attach(window->cursor_surface, buffer, 0, 0);
    wl_surface_damage(window->cursor_surface, 0, 0,
            image->width, image->height);
    wl_surface_commit(window->cursor_surface);
}

void Pointer::handle_frame()
{
    auto window = seat->window();
    window->setpointer(frame);
}

void Keyboard::handle_keymap(uint32_t format, int fd, uint32_t size)
{
    // todo: verify format is correct!

    auto buf = (char*) mmap(nullptr, size,
            PROT_READ, MAP_SHARED, fd, 0);
    if (buf == MAP_FAILED) {
        // todo: communicate error to wlwindow?
        LOGGER()->error("failed to mmap keymap for {}, {}",
                size, strerror(errno));
        close(fd);
        return;
    }

    if (keymap)
        xkb_keymap_unref(keymap);

    keymap = xkb_keymap_new_from_buffer(seat->window()->ctx,
            buf, size - 1, XKB_KEYMAP_FORMAT_TEXT_V1,
            XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(buf, size);
    close(fd);
    if (!keymap) {
        // todo: communicate error to wlwindow?
        LOGGER()->error("failed to compile keymap!");
        return;
    }

    if (state)
        xkb_state_unref(state);

    state = xkb_state_new(keymap);
    if (!state) {
        // todo: communicate error to wlwindow?
        LOGGER()->error("failed to create XKB state!");
        return;
    }

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

void Keyboard::handle_enter(uint32_t serial, wl_surface* surface,
        wl_array* keys)
{
    auto window = seat->window();
    window->setkbdfocus(true);
}

void Keyboard::handle_leave(uint32_t serial, wl_surface* surface)
{
    auto window = seat->window();
    window->setkbdfocus(false);
}

void Keyboard::handle_key(uint32_t serial, uint32_t time, uint32_t key,
        uint32_t state)
{
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return;

    // add 8, because that's how it works...it's in the docs somewhere
    key += 8;

    // todo: a bunch of this code is shared with xcbwindow...move
    // it somewhere common

    auto window = seat->window();
    process_key(key, window->m_term.get(), window->m_tty.get(),
            this->state, compose_state, seat->keymod);
}

void Keyboard::handle_modifiers(uint32_t serial, uint32_t mods_depressed,
        uint32_t mods_latched, uint32_t mods_locked,
        uint32_t group)
{
    xkb_state_update_mask(state, mods_depressed, mods_latched,
            mods_locked, 0, 0, group);

    // todo: keep these at object level instead of retrieving here?
    xkb_mod_index_t m_shift_modidx, m_ctrl_modidx, m_alt_modidx, m_logo_modidx;
    m_shift_modidx = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
    m_ctrl_modidx = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
    m_alt_modidx = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_ALT);
    m_logo_modidx = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_LOGO);

    // todo: this looks like it should be a single set operation after
    // determining mods, rather than reset and bit flipping
    seat->keymod.reset();
    if (xkb_state_mod_index_is_active(state, m_shift_modidx,
                XKB_STATE_MODS_EFFECTIVE) == 1)
        seat->keymod.set(term::MOD_SHIFT);
    if (xkb_state_mod_index_is_active(state, m_alt_modidx,
                XKB_STATE_MODS_EFFECTIVE) == 1)
        seat->keymod.set(term::MOD_ALT);
    if (xkb_state_mod_index_is_active(state, m_ctrl_modidx,
                XKB_STATE_MODS_EFFECTIVE) == 1)
        seat->keymod.set(term::MOD_CTRL);
    if (xkb_state_mod_index_is_active(state, m_logo_modidx,
                XKB_STATE_MODS_EFFECTIVE) == 1)
        seat->keymod.set(term::MOD_LOGO);
}

void Keyboard::handle_repeat_info(int32_t rate, int32_t delay)
{
    // todo: we should store the info here, then when a key is
    // pressed, add timer events to repeat the pressed key
}

void Keyboard::load_compose_table(const char* locale)
{
    if (compose_table)
        xkb_compose_table_unref(compose_table);

    // todo: cleanup compose_table
    compose_table = xkb_compose_table_new_from_locale(seat->window()->ctx,
            locale, (xkb_compose_compile_flags) 0);
    if (!compose_table) {
        LOGGER()->error("xkb_compose_table_new_from_locale failed");
        return;
    }

    if (compose_state)
        xkb_compose_state_unref(compose_state);

    // todo: cleanup compose_state
    compose_state = xkb_compose_state_new(
            compose_table, (xkb_compose_state_flags) 0);
    if (!compose_state)
        LOGGER()->error("xkb_compose_state_new failed");
}

void Surface::handle_enter(wl_output* output)
{
    window->visible = true;
    window->m_term->setdirty();
}

void Surface::handle_leave(wl_output* output)
{
    window->visible = false;
}

void Registry::handle_global(uint32_t name, const char* interface, uint32_t version)
{
    // LOGGER()->trace("Got a registry event for {} name {}", interface, name);

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        window->compositor = (wl_compositor*) wl_registry_bind(
                registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        auto wm_base = (xdg_wm_base*) wl_registry_bind(
                registry, name, &xdg_wm_base_interface, 2);
        window->wmbase = std::make_unique<XdgWmBase>(
                window, wm_base);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        // todo: handle multiple seats?
        auto seat = (wl_seat*) wl_registry_bind(
                registry, name, &wl_seat_interface, 6);
        window->seat = std::make_unique<Seat>(window, seat);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        // note: not adding a listener; all shms should support
        // argb8888, and so does cairo, so that's what we're going to use
        auto shm = (wl_shm*) wl_registry_bind(
                registry, name, &wl_shm_interface, 1);

        // note: this is a weird dependency on X settings. Also,
        // it's probably a bug, but if size == 1, it does the
        // right thing...no idea why, so not doing it.
        {
            int size = 32;
            auto v = std::getenv("XCURSOR_SIZE");
            // todo: std::from_chars
            if (v)
                size = atoi(v);

            // cursor_theme is an opaque pointer, and default_cursor
            // is a pointer into its data; cleaning up the former will
            // invalidate the latter
            // todo: do we have to do animated cursors? I don't want to
            window->cursor_theme = wl_cursor_theme_load(NULL, size, shm);
            window->default_cursor = wl_cursor_theme_get_cursor(
                    window->cursor_theme, "left_ptr");
        }

        window->buffers = std::make_unique<BufferPool>(shm);
    }
}

} // namespace wlwin

/// Returns a Window implemented with wayland
/// \param global bus
/// \return Window object
/// \addtogroup Window
/// @{
std::unique_ptr<Window> createWlWindow(std::shared_ptr<event::Bus> bus,
        std::shared_ptr<term::Term> term, std::shared_ptr<Tty> tty)
{
    return std::make_unique<wlwin::WlWindow>(std::move(bus),
            std::move(term), std::move(tty));
}
/// @}
