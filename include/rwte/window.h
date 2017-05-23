#ifndef SCREEN_H
#define SCREEN_H

#include <memory>

class WindowImpl;

class Window
{
public:
    Window();
    ~Window();

    bool create(int width, int height);
    void destroy();

    void resize(uint16_t width, uint16_t height);

    uint32_t windowid() const;

    // size in px
    uint16_t width() const;
    uint16_t height() const;

    // size in chars
    uint16_t rows() const;
    uint16_t cols() const;

    // todo: see if really needed
    uint16_t tw() const;
    uint16_t th() const;

    void draw();

    void settitle(const std::string& name);
    void seturgent(bool urgent);
    void bell(int volume);

    void setsel(char *sel); // assumes ownership
    void selpaste();
    void setclip(char *sel);
    void clippaste();

private:
    std::unique_ptr<WindowImpl> impl;
};

extern Window window;

#endif // SCREEN_H
