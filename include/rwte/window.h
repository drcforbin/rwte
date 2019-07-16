#ifndef RWTE_WINDOW_H
#define RWTE_WINDOW_H

#include "rwte/event.h"

#include <memory>

namespace term {
class Term;
}

/// \addtogroup Window
/// @{
class Window
{
public:
    virtual ~Window() { }

    virtual bool create(int cols, int rows) = 0;
    virtual void destroy() = 0;

    // todo: x specific?
    virtual uint32_t windowid() const = 0;

    virtual void draw() = 0;

    virtual void settitle(const std::string& name) = 0;
    virtual void seturgent(bool urgent) = 0;
    virtual void bell(int volume) = 0;

    // todo: x specific?
    virtual void setsel() = 0;
    virtual void selpaste() = 0;
    virtual void setclip() = 0;
    virtual void clippaste() = 0;
};

std::unique_ptr<Window> createXcbWindow(std::shared_ptr<event::Bus> bus,
        term::Term *term);
std::unique_ptr<Window> createWlWindow(std::shared_ptr<event::Bus> bus,
        term::Term *term);

extern std::unique_ptr<Window> window;
/// @}

#endif // RWTE_WINDOW_H
