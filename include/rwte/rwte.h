#ifndef RWTE_RWTE_H
#define RWTE_RWTE_H

#include "rwte/event.h"

#include <memory>
#include <string>
#include <vector>

namespace lua {
class State;
} // namespace lua
namespace reactor {
class ReactorCtrl;
} // namespace reactor
namespace term {
class Term;
} // namespace term
class Window;

struct Options
{
    std::vector<const char*> cmd;

    std::string title{"rwte"};
    std::string winname;
    std::string winclass;
    std::string font;
    std::string io;
    std::string line;
    bool noalt = false;
    bool throttledraw = true;
};

extern Options options;

class Rwte
{
public:
    Rwte(std::shared_ptr<event::Bus> bus, reactor::ReactorCtrl *ctrl);
    ~Rwte();

    void setWindow(std::shared_ptr<Window> window) { m_window = window; }
    void setTerm(std::shared_ptr<term::Term> term) { m_term = term; }

    void refresh();

    void start_blink();
    void stop_blink();

    void child_ended();

    void flushcb();
    void blinkcb();

    std::shared_ptr<lua::State> lua() { return m_lua; }

private:
    void onrefresh(const event::Refresh& evt);

    // todo: work out args
    void childcb();

    std::shared_ptr<event::Bus> m_bus;
    reactor::ReactorCtrl *m_ctrl;
    int m_refreshReg;

    std::shared_ptr<lua::State> m_lua;
    std::weak_ptr<Window> m_window;
    std::weak_ptr<term::Term> m_term;
};

// todo: refactor
extern std::unique_ptr<Rwte> rwte;

#endif // RWTE_RWTE_H
