#ifndef RWTE_RWTE_H
#define RWTE_RWTE_H

#include <cstdint>
#include <ev++.h>
#include <memory>
#include <string>
#include <vector>
#include <sys/types.h>

namespace lua
{
class State;
} // namespace lua

class Options
{
public:
    Options();

    std::vector<const char *> cmd;

    std::string title;
    std::string winname;
    std::string winclass;
    std::string font;
    std::string io;
    std::string line;
    bool noalt;
};

extern Options options;

class Rwte
{
public:
    Rwte();

    void resize(uint16_t width, uint16_t height);
    void watch_child(pid_t pid);

    void refresh();

    void start_blink();
    void stop_blink();

    std::shared_ptr<lua::State> lua() { return m_lua; }

private:
    void childcb(ev::child &w, int);
    void flushcb(ev::timer &w, int);
    void blinkcb(ev::timer &w, int);

    ev::child m_child;
    ev::timer m_flush;
    ev::timer m_blink;

    std::shared_ptr<lua::State> m_lua;
};

extern Rwte rwte;

#endif // RWTE_RWTE_H
