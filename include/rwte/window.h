#ifndef RWTE_WINDOW_H
#define RWTE_WINDOW_H

#include "rwte/event.h"

#include <memory>

class WindowImpl;

class Window
{
public:
    Window(std::shared_ptr<RwteBus> bus);
    ~Window();

    bool create(int cols, int rows);
    void destroy();

    uint32_t windowid() const;

    void draw();

    void settitle(const std::string& name);
    void seturgent(bool urgent);
    void bell(int volume);

    void setsel();
    void selpaste();
    void setclip();
    void clippaste();

private:
    std::unique_ptr<WindowImpl> impl;
};

extern std::unique_ptr<Window> window;

#endif // RWTE_WINDOW_H
