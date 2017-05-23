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

    std::string io;
    std::string line;
};

extern Options options;

class Rwte
{
public:
    Rwte();

    void resize(uint16_t width, uint16_t height);
    void watch_child(pid_t pid);

    void refresh();

    std::shared_ptr<LuaState> lua() { return m_lua; }

private:
    void childcb(ev::child &w, int);
    void flushcb(ev::timer &w, int);

    ev::child m_child;
    ev::timer m_flush;

    std::shared_ptr<LuaState> m_lua;
};

extern Rwte rwte;

#endif // RWTE_H
