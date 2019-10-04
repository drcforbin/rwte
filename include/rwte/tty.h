#ifndef RWTE_TTY_H
#define RWTE_TTY_H

#include "rwte/event.h"

#include <memory>
#include <string>

namespace term {
class Term;
}
class Window;

class TtyImpl;

class Tty
{
public:
    Tty(std::shared_ptr<event::Bus> bus, std::shared_ptr<term::Term> term);
    ~Tty();

    void open(Window* window);

    void write(std::string_view data);

    void print(std::string_view data);

    void hup();

private:
    std::unique_ptr<TtyImpl> impl;
};

#endif // RWTE_TTY_H
