#ifndef RWTE_H
#define RWTE_H

#include <sys/types.h>
#include <cstdint>
#include <string>
#include <memory>

#include <ev++.h>

class LuaState;

class Options
{
public:
    Options();

    const char ** cmd;

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

    std::shared_ptr<LuaState> lua() { return m_lua; }

private:
    void childcb(ev::child &w, int);
    void flushcb(ev::timer &w, int);
    void blinkcb(ev::timer &w, int);

    ev::child m_child;
    ev::timer m_flush;
    ev::timer m_blink;

    std::shared_ptr<LuaState> m_lua;
};

extern Rwte rwte;

#endif // RWTE_H
