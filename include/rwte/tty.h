#ifndef RWTE_TTY_H
#define RWTE_TTY_H

#include <memory>
#include <string>

class TtyImpl;

class Tty
{
public:
    Tty();
    ~Tty();

    void resize();

    void write(const std::string& data);
    void write(const char *data, std::size_t len);

    void print(const char *data, std::size_t len);

    void hup();

private:
    std::unique_ptr<TtyImpl> impl;
};

extern std::unique_ptr<Tty> g_tty;

#endif // RWTE_TTY_H
