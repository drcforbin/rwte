#include <xcb/xcb.h>
#include <cstdio>
#include <ev++.h>
#include <cstring>
#include <memory>
#include <wordexp.h>
#include <basedir.h>
#include <basedir_fs.h>

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

static void add_to_search_path(LuaState *L, const std::vector<std::string>& searchpaths, bool for_lua)
{
    if (L->type(-1) != LUA_TSTRING)
    {
        LOGGER()->warn("package.path is not a string");
        return;
    }

    for (auto& searchpath : searchpaths)
    {
        int components;

        L->pushstring(fmt::format(";{}{}", searchpath,
                for_lua? "/?.lua" : "/?.so"));

        if (for_lua)
        {
            L->pushstring(fmt::format(";{}/?/init.lua", searchpath,
                    for_lua? "/?.lua" : "/?.so"));

            // pushed two, concat them with string already on stack
            L->concat(3);
        }
        else
        {
            // pushed one, concat it with string already on stack
            L->concat(2);
        }
    }

    // add rwte lib path
    if (for_lua)
    {
        L->pushstring(
            ";" RWTE_LIB_PATH "/?.lua"
            ";" RWTE_LIB_PATH "/?/init.lua");
        L->concat(2);
    }
    else
    {
        L->pushstring(";" RWTE_LIB_PATH "/?.so");
        L->concat(2);
    }
}

static bool run_file(LuaState *L, const char *path)
{
    if (L->loadfile(path) || L->pcall(0, 0, 0))
    {
        LOGGER()->error("lua config error: {}", L->tostring(-1));
        L->pop(1);
        return false;
    }
    else
        return true;
}

static bool run_config(LuaState *L, xdgHandle *xdg, const char *confpatharg)
{
    // try specified path first
    if (confpatharg && run_file(L, confpatharg))
        return true;

    // try paths from xdgConfigFind
    char *paths = xdgConfigFind("rwte/config.lua", xdg);
    // paths from xdgConfigFind are null-terminated, with
    // empty string at the end (double null)
    char *tmp = paths;
    while (*tmp)
    {
        if (run_file(L, tmp))
            return true;
        tmp += std::strlen(tmp) + 1;
    }
    std::free(paths);

    // finally try CONFIG_FILE
    // use wordexp to expand possible ~ in the path
    wordexp_t exp_result;
    wordexp(CONFIG_FILE, &exp_result, 0);
    bool result = run_file(L, exp_result.we_wordv[0]);
    wordfree(&exp_result);

    return result;
}

int main()
{
    // todo: parse options
    options.cmd = 0;
    // hack: remove this
    options.io = "termoutput.bin";
    // todo: add a --config option to override config file
    const char *confpath = nullptr;

    auto L = rwte.lua();

    // register internal modules, logging first
    register_lualogging(L.get());
    register_luaterm(L.get());

    {
        // Get XDG basedir data
        xdgHandle xdg;
        xdgInitHandle(&xdg);

        // make a list of pachage search paths
        std::vector<std::string> searchpaths;
        const char *const *xdgconfigdirs = xdgSearchableConfigDirectories(&xdg);
        for(; *xdgconfigdirs; xdgconfigdirs++)
        {
            // append /rwte to each dir
            std::string path = *xdgconfigdirs;
            path += "/rwte";
            searchpaths.push_back(path);
        }

        // add search paths to lua world
        L->getglobal("package");
        if (L->istable(-1))
        {
            L->getfield(-1, "path");
            add_to_search_path(L.get(), searchpaths, true);
            L->setfield(-2, "path");

            L->getfield(-1, "cpath");
            add_to_search_path(L.get(), searchpaths, false);
            L->setfield(-2, "cpath");
        }
        else
            LOGGER()->error("package is not a table");
        L->pop();

        // find and run configuration file
        if (!run_config(L.get(), &xdg, confpath))
            LOGGER()->fatal("could not find/run config.lua");

        xdgWipeHandle(&xdg);
    }

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

    // get ready, loop!
    ev::default_loop main_loop;

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
