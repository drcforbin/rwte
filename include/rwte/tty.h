#ifndef RWTE_TTY_H
#define RWTE_TTY_H

#include "rwte/event.h"

#include <memory>
#include <string_view>

namespace reactor {
class ReactorCtrl;
}
namespace term {
class Term;
}
class Window;

class TtyImpl;

class Tty
{
public:
    // todo: make bus a raw ptr, since it isn't affected by lifetime?
    Tty(std::shared_ptr<event::Bus> bus,
            reactor::ReactorCtrl *ctrl,
            std::shared_ptr<term::Term> term);
    ~Tty();

    void open(Window* window);
    int fd() const;

    void write(std::string_view data);

    void print(std::string_view data);

    void hup();

    void read_ready();
    void write_ready();

private:
    std::unique_ptr<TtyImpl> impl;
};

#endif // RWTE_TTY_H
