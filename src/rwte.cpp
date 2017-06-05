#include <xcb/xcb.h>
#include <cstdio>
#include <ev++.h>
#include <cstring>
#include <memory>
#include <wordexp.h>
#include <basedir.h>
#include <basedir_fs.h>
#include <getopt.h>

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
#include "rwte/version.h"

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

// default values to use if we don't have
// a default value in config
static const float DEFAULT_BLINK_RATE = 0.6;

Options::Options() :
    cmd(0),
    title("rwte"),
    noalt(false)
{ }

Rwte::Rwte() :
    m_lua(std::make_shared<LuaState>())
{
    m_lua->openlibs();

    m_child.set<Rwte,&Rwte::childcb>(this);
    m_flush.set<Rwte,&Rwte::flushcb>(this);
    m_blink.set<Rwte,&Rwte::blinkcb>(this);
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

void Rwte::start_blink()
{
    if (!m_blink.is_active())
    {
        m_lua->getglobal("config");
        m_lua->getfield(-1, "blink_rate");

        int isnum = 0;
        float rate = m_lua->tonumberx(-1, &isnum);
        if (!isnum)
            rate = DEFAULT_BLINK_RATE;

        m_lua->pop(2);

        m_blink.start(rate, rate);
    }
    else
    {
        // reset the timer if it's already active
        // (so we don't blink until idle)
        m_blink.stop();
        m_blink.start();
    }
}

void Rwte::stop_blink()
{
    if (m_blink.is_active())
        m_blink.stop();
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

void Rwte::blinkcb(ev::timer &, int)
{
    g_term->blink();
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
    if (confpatharg)
    {
        if (run_file(L, confpatharg))
            return true;
        else
            LOGGER()->warn("unable to run specified config ({}); "
                    "running config.lua", confpatharg);
    }

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

static void exit_help(int code)
{
    fprintf((code == EXIT_SUCCESS) ? stdout : stderr,
        "Usage: rwte [options] [-- args]\n"
        "  -c, --config FILE     overrides config file\n"
        "  -a, --noalt           disables alt screens\n"
        "  -f, --font FONT       pango font string\n"
        "  -g, --geometry GEOM   window geometry; colsxrows, e.g.,\n"
        "                        \"80x24\" (the default)\n"
        "  -t, --title TITLE     window title; defaults to rwte\n"
        "  -n, --name NAME       window name; defaults to $TERM\n"
        "  -w, --winclass CLASS  overrides window class\n"
        "  -e, --exe COMMAND     command to execute instead of shell;\n"
        "                        if specified, any arguments to the\n"
        "                        command may be specified after a \"--\"\n"
        "  -o, --out OUT         writes all io to this file;\n"
        "                        \"-\" means stdout\n"
        "  -l, --line LINE       use a tty line instead of creating a\n"
        "                        new pty; LINE is expected to be the\n"
        "                        device\n"
        "  -h, --help            show help\n"
        "  -b, --bench           run config and exit\n"
        "  -v, --version         show version and exit\n");
    exit(code);
}

static void exit_version()
{
    fprintf(stdout, "rwte %s\n", version_string());
    exit(EXIT_SUCCESS);
}

static bool parse_geometry(const char *g, int *cols, int *rows)
{
    // parse cols
    char *end = nullptr;
    int c = strtol(g, &end, 10);
    if (c > 0 && *end == 'x')
    {
        // move past x
        end++;

        // parse rows
        int r = strtol(end, &end, 10);
        if (r > 0 && *end == 0)
        {
            *cols = c;
            *rows = r;
            return true;
        }
    }

    return false;
}

int main(int argc, char *argv[])
{
    auto L = rwte.lua();

    // register internal modules, logging first
    register_lualogging(L.get());
    register_luaterm(L.get());

    // feed lua our args
    L->newtable();
    for (int i = 0; i < argc; i++)
    {
        L->pushstring(argv[i]);
        L->seti(-2, i+1);
    }
    L->setglobal("args");

    static struct option long_options[] =
    {
        {"config", required_argument, nullptr, 'c'},
        {"winclass", required_argument, nullptr, 'w'},
        {"noalt", no_argument, nullptr, 'a'},
        {"font", required_argument, nullptr, 'f'},
        {"geometry", required_argument, nullptr, 'g'}, // colsxrows, e.g., 80x24
        {"title", required_argument, nullptr, 't'},
        {"name", required_argument, nullptr, 'n'},
        {"exe", required_argument, nullptr, 'e'},
        {"out", required_argument, nullptr, 'o'},
        {"line", required_argument, nullptr, 'l'},
        {"help", no_argument, nullptr, 'h'},
        {"bench", no_argument, nullptr, 'b'},
        {"version", no_argument, nullptr, 'v'},
        {nullptr, 0, nullptr, 0}
    };

    const char *confpath = nullptr;

    int opt;
    int cols = 0, rows = 0;
    bool got_exe = false;
    bool got_bench = false;
    bool got_title = false;
    while ((opt = getopt_long(argc, argv, "-c:w:af:g:t:n:o:l:hbve:",
                long_options, NULL)) != -1)
    {
        switch (opt)
        {
        case 'c':
            confpath = optarg;
            break;
        case 'w':
            options.winclass = optarg;
            break;
        case 'a':
            options.noalt = true;
            break;
        case 'f':
            options.font = optarg;
            break;
        case 'g':
            if (!parse_geometry(optarg, &cols, &rows))
                LOGGER()->warn("ignoring invalid geometry '{}'", optarg);
            break;
        case 't':
            options.title = optarg;
            got_title = true;
            break;
        case 'n':
            options.winname = optarg;
            break;
        case 'o':
            options.io = optarg;
            break;
        case 'l':
            options.line = optarg;
            break;
        case 'h':
            exit_help(EXIT_SUCCESS);
            break;
        case 'b':
            got_bench = true;
            break;
        case 'v':
            exit_version();
            break;
        case 'e':
            // todo: handle -e
            LOGGER()->info("exe: {}", optarg);
            got_exe = true;
            break;
        case 1:
            fprintf(stderr, "%s: invalid arg -- '%s'\n",
                    argv[0], argv[optind-1]);
        default:
            exit_help(EXIT_FAILURE);
        }
    }

    // todo: handle these with -e
    if (optind < argc)
    {
        LOGGER()->info("non-option args:");
        while (optind < argc)
            LOGGER()->info("{}", argv[optind++]);
    }

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

    // nothing else to do if bench arg was specified
    if (got_bench)
        return 0;

    // if a title was passed on command line, then
    // use that rather than checking lua config
    if (!got_title)
    {
        L->getglobal("config");
        if (L->istable(-1))
        {
            L->getfield(-1, "title");
            std::string title = L->tostring(-1);
            if (!title.empty())
                options.title = title;
            L->pop();
        }
        else
            LOGGER()->fatal("expected 'config' to be table");
        L->pop();
    }

    // get ready, loop!
    ev::default_loop main_loop;

    // get cols and rows, default to 80x24
    if (cols == 0 || rows == 0)
    {
        L->getglobal("config");
        L->getfield(-1, "default_cols");
        cols = L->tointegerdef(-1, 80);
        L->getfield(-2, "default_rows");
        rows = L->tointegerdef(-1, 24);
        L->pop(3);
    }

    cols = MAX(cols, 1);
    rows = MAX(rows, 1);

    g_term = std::make_unique<Term>(cols, rows);

    // todo: width and height are arbitrary
    if (!window.create(cols, rows))
        return 1;

    {
        SigWatcher sigwatcher;
        main_loop.run();
    }

    window.destroy();

    LOGGER()->debug("exiting");
    return 0;
}
