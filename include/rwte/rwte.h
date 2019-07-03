#ifndef RWTE_RWTE_H
#define RWTE_RWTE_H

#include "rwte/event.h"

#include <ev++.h>
#include <memory>
#include <string>
#include <vector>

namespace lua
{
class State;
} // namespace lua

struct Options
{
    std::vector<const char *> cmd;

    std::string title {"rwte"};
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
    Rwte(std::shared_ptr<RwteBus> bus);
    ~Rwte();

    void watch_child(pid_t pid);

    void refresh();

    void start_blink();
    void stop_blink();

    std::shared_ptr<lua::State> lua() { return m_lua; }

private:
    void onrefresh(const RefreshEvt& evt);

    void childcb(ev::child &w, int);
    void flushcb(ev::timer &w, int);
    void blinkcb(ev::timer &w, int);

    std::shared_ptr<RwteBus> m_bus;
    int m_refreshReg;

    ev::child m_child;
    ev::timer m_flush;
    ev::timer m_blink;

    std::shared_ptr<lua::State> m_lua;
};

// todo: refactor
extern std::unique_ptr<Rwte> rwte;

#endif // RWTE_RWTE_H
