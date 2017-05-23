#include <xcb/xcb.h>
#include <cstdio>
#include <ev++.h>
#include <cstring>
#include <memory>
#include <wordexp.h>

#include "rwte/config.h"
#include "rwte/renderer.h"
#include "rwte/rwte.h"
#include "rwte/logging.h"
#include "rwte/sigwatcher.h"
#include "rwte/term.h"
#include "rwte/tty.h"
#include "rwte/window.h"
#include "rwte/luastate.h"
#include "rwte/lualogging.h"
#include "rwte/luaterm.h"

// globals
Window window;
Options options;
Rwte rwte;
std::unique_ptr<Term> g_term;
std::unique_ptr<Tty> g_tty;
lua_State * g_L = NULL;

#define LOGGER() (logging::get("rwte"))

#define MIN(a, b) ((a) < (b)? (a) : (b))
#define MAX(a, b) ((a) < (b)? (b) : (a))

Options::Options() :
    title("rwte")
{ }

Rwte::Rwte() :
    m_lua(std::make_shared<LuaState>())
{
    m_lua->openlibs();

    m_child.set<Rwte,&Rwte::childcb>(this);
    m_flush.set<Rwte,&Rwte::flushcb>(this);
}

void Rwte::resize(uint16_t width, uint16_t height)
{
    if (width == 0)
        width = window.width();
    if (height == 0)
        height = window.height();

    if (window.width() != width || window.height() != height)
    {
        window.resize(width, height);
        g_term->resize(window.cols(), window.rows());

        if (g_tty)
            g_tty->resize();
    }
}

void Rwte::watch_child(pid_t pid)
{
    LOGGER()->debug("watching child {}", pid);
    m_child.start(pid);
}

void Rwte::refresh()
{
    if (!m_flush.is_active())
        m_flush.start(1.0/60.0);
}

void Rwte::childcb(ev::child &w, int)
{
    if (!WIFEXITED(w.rstatus) || WEXITSTATUS(w.rstatus))
        LOGGER()->warn("child finished with error {}", w.rstatus);
    else
        LOGGER()->info("child exited");
    w.loop.break_loop(ev::ALL);
}

void Rwte::flushcb(ev::timer &, int)
{
    window.draw();
}

int main()
{
    auto L = rwte.lua();

    // register modules, logging first
    register_lualogging(L.get());
    register_luaterm(L.get());

    {
        // use wordexp to expand possible ~ in CONFIG_FILE
        wordexp_t exp_result;
        wordexp(CONFIG_FILE, &exp_result, 0);

        // load the config file
        if (L->loadfile(exp_result.we_wordv[0]) || L->pcall(0, 0, 0))
        {
            LOGGER()->fatal("lua config error: {}", L->tostring(-1));
            L->pop(1);
        }
        else
        {
            L->getglobal("config");
            if (L->istable(-1))
            {
                L->getfield(-1, "title");
                std::string title = L->tostring(-1);
                if (!title.empty())
                    options.title = title;
                L->pop(1);
            }
            else
                LOGGER()->fatal("expected 'config' to be table");
            L->pop(1);
        }

        wordfree(&exp_result);
    }

    // get ready, loop!
    ev::default_loop main_loop;

    // todo: parse options
    options.cmd = 0;
    // hack: remove this
    options.io = "termoutput.bin";

    L->getglobal("config");
    L->getfield(-1, "default_cols");
    int isnum = 0;
    int cols = L->tointegerx(-1, &isnum);
    if (!isnum)
        cols = 80;
    L->getfield(-2, "default_rows");
    int rows = L->tointegerx(-1, &isnum);
    if (!isnum)
        rows = 24;
    L->pop(3);

    g_term = std::make_unique<Term>(MAX(cols, 1), MAX(rows, 1));

    // todo: width and height are arbitrary
    if (!window.create(300, 200))
        return 1;

    {
        SigWatcher sigwatcher;
        main_loop.run();
    }

    window.destroy();

    LOGGER()->debug("exiting");
    return 0;
}
