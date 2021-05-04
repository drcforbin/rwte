#ifndef RWTE_WINDOW_H
#define RWTE_WINDOW_H

#include "rwte/buildopt.h"
#include "rwte/event.h"

#include <memory>
#include <string>

namespace reactor {
class ReactorCtrl;
}
namespace term {
class Term;
}
class Tty;

// todo: namespaceify

/// \addtogroup Window
/// @{
class WindowError : public std::runtime_error
{
public:
    explicit WindowError(const std::string& arg);
    explicit WindowError(const char* arg);

    WindowError(const WindowError&) = default;
    WindowError& operator=(const WindowError&) = default;
    WindowError(WindowError&&) = default;
    WindowError& operator=(WindowError&&) = default;

    virtual ~WindowError();
};

class Window
{
public:
    virtual ~Window() {}

#if !defined(BUILD_WAYLAND_ONLY)
    virtual uint32_t windowid() const = 0;
#endif

    virtual int fd() const = 0;
    virtual void prepare() = 0;
    virtual bool event() = 0;
    virtual bool check() = 0;

    virtual void draw() = 0;

    virtual void settitle(std::string_view name) = 0;
    virtual void seturgent(bool urgent) = 0;
    virtual void bell(int volume) = 0;

    // todo: x specific?
    virtual void setsel() = 0;
    virtual void selpaste() = 0;
    virtual void setclip() = 0;
    virtual void clippaste() = 0;
};

std::unique_ptr<Window> createXcbWindow(std::shared_ptr<event::Bus> bus,
        std::shared_ptr<term::Term> term, std::shared_ptr<Tty> tty);
std::unique_ptr<Window> createWlWindow(std::shared_ptr<event::Bus> bus,
        reactor::ReactorCtrl *ctrl,
        std::shared_ptr<term::Term> term, std::shared_ptr<Tty> tty);

/// @}

#endif // RWTE_WINDOW_H
