#ifndef RWTE_WAYLAND_H
#define RWTE_WAYLAND_H

#include <memory>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xdg-shell/xdg-shell-client-protocol.h"

#define LOGGER() (logging::get("wayland"))

class Shm;
struct xkb_keymap;
struct xkb_state;

namespace wayland {

template<class T>
class Buffer {
    using Self = Buffer<T>;
public:
    Buffer(wl_buffer *buffer) :
        m_buffer(buffer)
    {
        // LOGGER()->debug("Buffer ctor 0x{:08X}", (uint64_t) buffer);
        wl_buffer_add_listener(m_buffer, &listener, this);
    }

    ~Buffer() {
        wl_buffer_destroy(m_buffer);
    }

    /*
    Buffer(const Buffer& other) :
        Buffer(other.m_buffer)
    {}

    Buffer(Buffer&& other) noexcept :
        m_buffer(std::exchange(other.m_buffer, nullptr))
    {}

    Buffer& operator=(const Buffer& other)
    {
         return *this = Buffer(m_buffer);
    }

    Buffer& operator=(Buffer&& other) noexcept
    {
        std::swap(m_buffer, other.m_buffer);
        return *this;
    }
    */

    bool operator==(const Buffer& other) const {
        return m_buffer == other.m_buffer;
    }

    bool operator!=(const Buffer& other) const {
        return m_buffer != other.m_buffer;
    }

    // todo: find better way
    wl_buffer *get() { return m_buffer; }

protected:
    void handle_release() { }

    wl_buffer *m_buffer;

private:
    static void handle_release(void *data, struct wl_buffer *wl_buffer) {
        // LOGGER()->debug("Buffer handle_release");
        static_cast<T*>(data)->handle_release();
    }

    static const struct wl_buffer_listener listener;
};

template<class T>
const struct wl_buffer_listener Buffer<T>::listener = {
    &Buffer<T>::handle_release
};

template<class T>
class Pointer {
public:
    Pointer(wl_pointer *pointer) :
        pointer(pointer)
    {
        // LOGGER()->debug("Pointer ctor 0x{:08X}", (uint64_t) pointer);
        wl_pointer_add_listener(pointer, &listener, this);
    }

    ~Pointer() {
        wl_pointer_destroy(pointer);
    }

protected:
    void handle_enter(uint32_t serial, struct wl_surface *surface,
                wl_fixed_t sx, wl_fixed_t sy) { }
    void handle_leave(uint32_t serial, struct wl_surface *surface) { }
    void handle_motion(uint32_t time, wl_fixed_t sx, wl_fixed_t sy) { }
    void handle_button(uint32_t serial, uint32_t time, uint32_t button,
                uint32_t state) { }
    void handle_axis(uint32_t time, uint32_t axis, wl_fixed_t value) { }
    void handle_frame() { }
    void handle_axis_source(uint32_t axis_source) { }
    void handle_axis_stop(uint32_t time, uint32_t axis) { }
    void handle_axis_discrete(uint32_t axis, int32_t discrete) { }

    wl_pointer *pointer;

private:
    static void handle_enter(void *data, struct wl_pointer *pointer,
                uint32_t serial, struct wl_surface *surface,
                wl_fixed_t sx, wl_fixed_t sy) {
        static_cast<T*>(data)->handle_enter(serial, surface, sx, sy);
    }

    static void handle_leave(void *data, struct wl_pointer *pointer,
                uint32_t serial, struct wl_surface *surface) {
        static_cast<T*>(data)->handle_leave(serial, surface);
    }

    static void handle_motion(void *data, struct wl_pointer *pointer,
                uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
        static_cast<T*>(data)->handle_motion(time, sx, sy);
    }

    static void handle_button(void *data, struct wl_pointer *pointer,
                uint32_t serial, uint32_t time, uint32_t button,
                uint32_t state) {
        static_cast<T*>(data)->handle_button(serial, time, button, state);
    }

    static void handle_axis(void *data, struct wl_pointer *pointer,
                uint32_t time, uint32_t axis, wl_fixed_t value) {
        static_cast<T*>(data)->handle_axis(time, axis, value);
    }

    static void handle_frame(void *data, struct wl_pointer *pointer) {
        static_cast<T*>(data)->handle_frame();
    }

    static void handle_axis_source(void *data, struct wl_pointer *pointer,
                    uint32_t axis_source) {
        static_cast<T*>(data)->handle_axis_source(axis_source);
    }

    static void handle_axis_stop(void *data, struct wl_pointer *pointer,
                  uint32_t time, uint32_t axis) {
        static_cast<T*>(data)->handle_axis_stop(time, axis);
    }

    static void handle_axis_discrete(void *data, struct wl_pointer *wl_pointer,
                  uint32_t axis, int32_t discrete) {
        static_cast<T*>(data)->handle_axis_discrete(axis, discrete);
    }

    static const struct wl_pointer_listener listener;
};

template<class T>
const struct wl_pointer_listener Pointer<T>::listener = {
    &Pointer<T>::handle_enter,
    &Pointer<T>::handle_leave,
    &Pointer<T>::handle_motion,
    &Pointer<T>::handle_button,
    &Pointer<T>::handle_axis,
    &Pointer<T>::handle_frame,
    &Pointer<T>::handle_axis_source,
    &Pointer<T>::handle_axis_stop,
    &Pointer<T>::handle_axis_discrete
};

template<class T>
class Keyboard {
public:
    Keyboard(wl_keyboard *keyboard) :
        keyboard(keyboard)
    {
        // LOGGER()->debug("Keyboard ctor 0x{:08X}", (uint64_t) keyboard);
        wl_keyboard_add_listener(keyboard, &listener, this);
    }

    ~Keyboard() {
        // todo: wl_keyboard_destroy or wl_keyboard_release?
        wl_keyboard_destroy(keyboard);
    }

protected:
    void handle_keymap(uint32_t format, int fd, uint32_t size) { }
    void handle_enter(uint32_t serial, struct wl_surface *surface,
                  struct wl_array *keys) { }
    void handle_leave(uint32_t serial, struct wl_surface *surface) { }
    void handle_key(uint32_t serial, uint32_t time, uint32_t key,
                uint32_t state) { }
    void handle_modifiers(uint32_t serial, uint32_t mods_depressed,
                  uint32_t mods_latched, uint32_t mods_locked,
                  uint32_t group) { }
    void handle_repeat_info(int32_t rate, int32_t delay) { }

    wl_keyboard *keyboard;

private:
    static void handle_keymap(void *data, struct wl_keyboard *keyboard,
                   uint32_t format, int fd, uint32_t size) {
        static_cast<T*>(data)->handle_keymap(format, fd, size);
    }

    static void handle_enter(void *data, struct wl_keyboard *keyboard,
                  uint32_t serial, struct wl_surface *surface,
                  struct wl_array *keys) {
        static_cast<T*>(data)->handle_enter(serial, surface, keys);
    }

    static void handle_leave(void *data, struct wl_keyboard *keyboard,
                  uint32_t serial, struct wl_surface *surface) {
        static_cast<T*>(data)->handle_leave(serial, surface);
    }

    static void handle_key(void *data, struct wl_keyboard *keyboard,
                uint32_t serial, uint32_t time, uint32_t key,
                uint32_t state) {
        static_cast<T*>(data)->handle_key(serial, time, key, state);
    }

    static void handle_modifiers(void *data, struct wl_keyboard *keyboard,
              uint32_t serial, uint32_t mods_depressed,
              uint32_t mods_latched, uint32_t mods_locked,
              uint32_t group) {
        static_cast<T*>(data)->handle_modifiers(serial, mods_depressed,
                mods_latched, mods_locked, group);
    }

    static void handle_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
            int32_t rate, int32_t delay) {
        static_cast<T*>(data)->handle_repeat_info(rate, delay);
    }

    static const struct wl_keyboard_listener listener;
};

template<class T>
const struct wl_keyboard_listener Keyboard<T>::listener = {
    &Keyboard<T>::handle_keymap,
    &Keyboard<T>::handle_enter,
    &Keyboard<T>::handle_leave,
    &Keyboard<T>::handle_key,
    &Keyboard<T>::handle_modifiers,
    &Keyboard<T>::handle_repeat_info
};

template<class T>
class Touch {
public:
    Touch(wl_touch *touch) :
        touch(touch)
    {
        // LOGGER()->debug("Touch ctor 0x{:08X}", (uint64_t) touch);
        wl_touch_add_listener(touch, &listener, this);
    }

    ~Touch() {
        wl_touch_destroy(touch);
    }

protected:
    void handle_down(uint32_t serial, uint32_t time, struct wl_surface *surface,
              int32_t id, wl_fixed_t x_w, wl_fixed_t y_w) { }
    void handle_up(uint32_t serial, uint32_t time, int32_t id) { }
    void handle_motion(uint32_t time, int32_t id,
            wl_fixed_t x_w, wl_fixed_t y_w) { }
    void handle_frame() { }
    void handle_cancel() { }
    void handle_shape(int32_t id, wl_fixed_t major, wl_fixed_t minor) { }
    void handle_orientation( int32_t id, wl_fixed_t orientation) { }

    wl_touch *touch;

private:
    static void handle_down(void *data, struct wl_touch *wl_touch,
              uint32_t serial, uint32_t time, struct wl_surface *surface,
              int32_t id, wl_fixed_t x_w, wl_fixed_t y_w) {
        static_cast<T*>(data)->handle_down(serial, time, surface,
                id, x_w, y_w);
    }

    static void handle_up(void *data, struct wl_touch *wl_touch,
            uint32_t serial, uint32_t time, int32_t id) {
        static_cast<T*>(data)->handle_up(serial, time, id);
    }

    static void handle_motion(void *data, struct wl_touch *wl_touch,
                uint32_t time, int32_t id, wl_fixed_t x_w, wl_fixed_t y_w) {
        static_cast<T*>(data)->handle_motion(time, id, x_w, y_w);
    }

    static void handle_frame(void *data, struct wl_touch *wl_touch) {
        static_cast<T*>(data)->handle_frame();
    }

    static void handle_cancel(void *data, struct wl_touch *wl_touch) {
        static_cast<T*>(data)->handle_cancel();
    }

    static void handle_shape(void *data, struct wl_touch *wl_touch,
            int32_t id, wl_fixed_t major, wl_fixed_t minor) {
        static_cast<T*>(data)->handle_shape(id, major, minor);
    }

    static void handle_orientation(void *data, struct wl_touch *wl_touch,
            int32_t id, wl_fixed_t orientation) {
        static_cast<T*>(data)->handle_orientation(id, orientation);
    }

    static const struct wl_touch_listener listener;
};

template<class T>
const struct wl_touch_listener Touch<T>::listener = {
    &Touch<T>::handle_down,
    &Touch<T>::handle_up,
    &Touch<T>::handle_motion,
    &Touch<T>::handle_frame,
    &Touch<T>::handle_cancel,
    &Touch<T>::handle_shape,
    &Touch<T>::handle_orientation
};

template<
    class T,
    class PointerT,
    class KeyboardT,
    class TouchT>
class Seat {
public:
    Seat(wl_seat *seat) :
        seat(seat)
    {
        // LOGGER()->debug("Seat ctor 0x{:08X}", (uint64_t) seat);
        wl_seat_add_listener(seat, &listener, this);
    }

    ~Seat() {
        wl_seat_release(seat);
    }

protected:
    void handle_capabilities(uint32_t caps) { }
    void handle_name(const char *name) { }

    wl_seat *seat;

    std::unique_ptr<PointerT> pointer;
    std::unique_ptr<KeyboardT> keyboard;
    std::unique_ptr<TouchT> touch;

private:
    static void handle_capabilities(void *data, struct wl_seat *wl_seat,
            uint32_t caps) {
        // LOGGER()->debug("Seat handle_capabilities");
        auto seat = static_cast<T*>(data);

        if ((caps & WL_SEAT_CAPABILITY_POINTER) && !seat->pointer) {
            seat->pointer = std::make_unique<PointerT>(
                    seat, wl_seat_get_pointer(wl_seat));
        }
        else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && seat->pointer) {
            seat->pointer.reset();
        }

        if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !seat->keyboard) {
            seat->keyboard = std::make_unique<KeyboardT>(
                    seat, wl_seat_get_keyboard(wl_seat));
        } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && seat->keyboard) {
            seat->keyboard.reset();
        }

        if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !seat->touch) {
            seat->touch = std::make_unique<TouchT>(
                    seat, wl_seat_get_touch(wl_seat));
        } else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && seat->touch) {
            seat->touch.reset();
        }

        seat->handle_capabilities(caps);
    }

    static void handle_name(void *data, struct wl_seat *wl_seat,
            const char *name) {
        // LOGGER()->debug("Seat handle_name");
        static_cast<T*>(data)->handle_name(name);
    }

    static const struct wl_seat_listener listener;
};

template<class T, class PointerT, class KeyboardT, class TouchT>
const struct wl_seat_listener Seat<T, PointerT, KeyboardT, TouchT>::listener = {
    &Seat<T, PointerT, KeyboardT, TouchT>::handle_capabilities,
    &Seat<T, PointerT, KeyboardT, TouchT>::handle_name
};

template<class T>
class XdgToplevel {
public:
    XdgToplevel(xdg_toplevel *toplevel) :
        toplevel(toplevel)
    {
        // LOGGER()->debug("XdgToplevel ctor 0x{:08X}", (uint64_t) toplevel);
        xdg_toplevel_add_listener(toplevel, &listener, this);
    }

    ~XdgToplevel() {
        xdg_toplevel_destroy(toplevel);
    }

    void set_title(const char *title) {
        // LOGGER()->debug("XdgToplevel set_title");
    	xdg_toplevel_set_title(toplevel, title);
    }

    void set_app_id(const char *app_id) {
        // LOGGER()->debug("XdgToplevel set_app_id");
        xdg_toplevel_set_app_id(toplevel, app_id);
    }

    void unset_fullscreen() {
        // LOGGER()->debug("XdgToplevel unset_fullscreen");
        xdg_toplevel_unset_fullscreen(toplevel);
    }

    void set_fullscreen() {
        // LOGGER()->debug("XdgToplevel set_fullscreen");
        xdg_toplevel_set_fullscreen(toplevel, nullptr);
    }

protected:
    void handle_configure(int32_t width, int32_t height,
            struct wl_array *states) { }
    void handle_close() { }

    xdg_toplevel *toplevel;

private:
    static void handle_configure(void *data, struct xdg_toplevel *xdg_toplevel,
            int32_t width, int32_t height, struct wl_array *states) {
        // LOGGER()->debug("XdgToplevel handle_configure");
        static_cast<T*>(data)->handle_configure(width, height, states);
    }

    static void handle_close(void *data, struct xdg_toplevel *xdg_toplevel) {
        // LOGGER()->debug("XdgToplevel handle_close");
        static_cast<T*>(data)->handle_close();
    }

    static const struct xdg_toplevel_listener listener;
};

template<class T>
const struct xdg_toplevel_listener XdgToplevel<T>::listener = {
    &XdgToplevel<T>::handle_configure,
    &XdgToplevel<T>::handle_close
};

template<class T, class WindowT, class XdgToplevelT>
class XdgSurface {
    using Self = XdgSurface<T, WindowT, XdgToplevelT>;
public:
    XdgSurface(WindowT *window, xdg_surface *surface) :
        window(window),
        surface(surface)
    {
        // LOGGER()->debug("XdgSurface ctor 0x{:08X}", (uint64_t) surface);
        xdg_surface_add_listener(surface, &listener, this);
    }

    ~XdgSurface() {
        xdg_surface_destroy(surface);
    }

    std::unique_ptr<XdgToplevelT> get_xdg_toplevel() {
        // LOGGER()->debug("XdgSurface get_xdg_toplevel");
        return std::make_unique<XdgToplevelT>(window,
                xdg_surface_get_toplevel(surface));
    }

protected:
    void handle_configure(uint32_t serial) { }

    WindowT *window;
    xdg_surface *surface;

private:
    static void handle_configure(void *data, struct xdg_surface *xdg_surface,
            uint32_t serial) {
        // LOGGER()->debug("XdgSurface handle_configure");
        static_cast<T*>(data)->handle_configure(serial);

        auto self = static_cast<Self *>(data);
        xdg_surface_ack_configure(self->surface, serial);
    }

    static const struct xdg_surface_listener listener;
};

template<class T, class WindowT, class XdgToplevelT>
const struct xdg_surface_listener XdgSurface<T, WindowT, XdgToplevelT>::listener = {
    &XdgSurface<T, WindowT, XdgToplevelT>::handle_configure
};

template<class T, class WindowT, class SurfaceT, class XdgSurfaceT>
class XdgWmBase {
    using Self = XdgWmBase<T, WindowT, SurfaceT, XdgSurfaceT>;
public:
    XdgWmBase(WindowT *window, xdg_wm_base *wmbase) :
        window(window),
        wmbase(wmbase)
    {
        // LOGGER()->debug("XdgWmBase ctor 0x{:08X}", (uint64_t) wmbase);
        xdg_wm_base_add_listener(wmbase, &listener, this);
    }

    ~XdgWmBase() {
        // release?
    }

    std::unique_ptr<XdgSurfaceT> get_xdg_surface(SurfaceT *surface) {
        // LOGGER()->debug("XdgWmBase get_xdg_surface");
        // todo: better way than this get?
        return std::make_unique<XdgSurfaceT>(window,
                xdg_wm_base_get_xdg_surface(wmbase, surface->get()));
    }

protected:
    void handle_ping(uint32_t serial) { }

    WindowT *window;
    xdg_wm_base *wmbase;

private:
    static void handle_ping(void *data, struct xdg_wm_base *xdg_wm_base,
            uint32_t serial) {
        // LOGGER()->debug("XdgWmBase handle_ping");
        static_cast<T*>(data)->handle_ping(serial);

        auto self = static_cast<Self *>(data);
        xdg_wm_base_pong(self->wmbase, serial);
    }

    static const struct xdg_wm_base_listener listener;
};

template<class T, class WT, class ST, class XST>
const struct xdg_wm_base_listener XdgWmBase<T, WT, ST, XST>::listener = {
    &XdgWmBase<T, WT, ST, XST>::handle_ping
};

template<class T>
class Registry {
public:
    Registry(wl_registry *registry) :
        registry(registry)
    {
        // LOGGER()->debug("Registry ctor 0x{:08X}", (uint64_t) registry);
        wl_registry_add_listener(registry, &listener, this);
    }

    ~Registry() {
        // todo: release?
    }

protected:
    void handle_global(uint32_t name, const char *interface,
            uint32_t version) { }
    void handle_global_remove(uint32_t name) { }

    wl_registry *registry;

private:
    static void handle_global(void *data, struct wl_registry *registry,
            uint32_t name, const char *interface, uint32_t version) {
        // LOGGER()->debug("Registry handle_global");
        static_cast<T*>(data)->handle_global(name, interface, version);
    }

    static void handle_global_remove(void *data,
            struct wl_registry *registry, uint32_t name) {
        LOGGER()->debug("Registry handle_global_remove");
        static_cast<T*>(data)->handle_global_remove(name);
    }

    static const struct wl_registry_listener listener;
};

template<class T>
const struct wl_registry_listener Registry<T>::listener = {
    &Registry<T>::handle_global,
    &Registry<T>::handle_global_remove
};

template<class T>
class Surface {
public:
    Surface(wl_surface *surface) :
        surface(surface)
    {
        LOGGER()->debug("Surface ctor 0x{:08X}", (uint64_t) surface);
        wl_surface_add_listener(surface, &listener, this);
    }

    ~Surface() {
        wl_surface_destroy(surface);
    }

    // hack
    wl_surface *get() {
        return surface;
    }

    void attach(wl_buffer *buffer, int x, int y) {
        // LOGGER()->debug("Surface attach");
        wl_surface_attach(surface, buffer, x, y);
    }

    void damage_buffer(int x, int y, int width, int height) {
        // LOGGER()->debug("Surface damage_buffer");
        wl_surface_damage_buffer(surface, x, y, width, height);
    }

    void commit() {
        // LOGGER()->debug("Surface commit");
        wl_surface_commit(surface);
    }

protected:
    void handle_enter(struct wl_output *output) { }
    void handle_leave(struct wl_output *output) { }

    wl_surface *surface;

private:
    static void handle_enter(void *data, struct wl_surface *wl_surface,
            struct wl_output *output) {
        // LOGGER()->debug("Surface handle_enter");
        static_cast<T*>(data)->handle_enter(output);
    }

    static void handle_leave(void *data, struct wl_surface *wl_surface,
            struct wl_output *output) {
        // LOGGER()->debug("Surface handle_leave");
        static_cast<T*>(data)->handle_leave(output);
    }

    static const struct wl_surface_listener listener;
};

template<class T>
const struct wl_surface_listener Surface<T>::listener = {
    &Surface<T>::handle_enter,
    &Surface<T>::handle_leave
};

} // namespace wayland

#undef LOGGER

#endif // RWTE_WAYLAND_H
