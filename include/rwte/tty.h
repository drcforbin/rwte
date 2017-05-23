#ifndef TTY_H
#define TTY_H

#include <memory>

class TtyImpl;

class Tty
{
public:
    Tty();
    ~Tty();

    void resize();

    void write(const char *data, std::size_t len);
    void print(const char *data, std::size_t len);

    void hup();

private:
    std::unique_ptr<TtyImpl> impl;
};

extern std::unique_ptr<Tty> g_tty;

#endif // TTY_H
