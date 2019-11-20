#include "lua/logging.h"
#include "lua/state.h"
#include "lua/term.h"
#include "lua/window.h"
#include "rw/argparse.h"
#include "rw/logging.h"
#include "rwte/config.h"
#include "rwte/event.h"
#include "rwte/rwte.h"
#include "rwte/sigwatcher.h"
#include "rwte/tty.h"
#include "rwte/version.h"
#include "rwte/window.h"

#include <algorithm>
#include <basedir.h>
#include <basedir_fs.h>
#include <getopt.h>
#include <vector>
#include <wordexp.h>

#define LOGGER() (rw::logging::get("rwte-main"))

using namespace std::literals;

static void add_to_search_path(lua::State* L, const std::vector<std::string>& searchpaths, bool for_lua)
{
    if (L->type(-1) != LUA_TSTRING) {
        LOGGER()->warn("package.path is not a string");
        return;
    }

    for (auto& searchpath : searchpaths) {
        L->pushstring(fmt::format(";{}{}", searchpath,
                for_lua ? "/?.lua" : "/?.so"));

        if (for_lua) {
            L->pushstring(fmt::format(";{}/?/init.lua", searchpath,
                    for_lua ? "/?.lua" : "/?.so"));

            // pushed two, concat them with string already on stack
            L->concat(3);
        } else {
            // pushed one, concat it with string already on stack
            L->concat(2);
        }
    }

    // add rwte lib path
    if (for_lua) {
        L->pushstring(
                ";" RWTE_LIB_PATH
                "/?.lua"
                ";" RWTE_LIB_PATH "/?/init.lua");
        L->concat(2);
    } else {
        L->pushstring(";" RWTE_LIB_PATH "/?.so");
        L->concat(2);
    }
}

static bool run_file(lua::State* L, const char* path)
{
    if (L->loadfile(path) || L->pcall(0, 0, 0)) {
        LOGGER()->error("lua config error: {}", L->tostring(-1));
        L->pop(1);
        return false;
    } else
        return true;
}

static bool run_config(lua::State* L, xdgHandle* xdg,
        const char* confpatharg)
{
    // try specified path first
    if (confpatharg) {
        if (run_file(L, confpatharg))
            return true;
        else
            LOGGER()->warn(
                    "unable to run specified config ({}); "
                    "running config.lua",
                    confpatharg);
    }

    // try paths from xdgConfigFind
    char* paths = xdgConfigFind("rwte/config.lua", xdg);
    // paths from xdgConfigFind are null-terminated, with
    // empty string at the end (double null)
    char* tmp = paths;
    while (*tmp) {
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

auto usage = std::string_view(R"(
Usage: rwte [options] [-- args]
  -c, --config FILE     overrides config file
  -a, --noalt           disables alt screens
  -f, --font FONT       pango font string
  -g, --geometry GEOM   window geometry; colsxrows, e.g.,
                        \"80x24\" (the default)
  -t, --title TITLE     window title; defaults to rwte
  -n, --name NAME       window name; defaults to $TERM
  -w, --winclass CLASS  overrides window class
  -e, --exe COMMAND     command to execute instead of shell;
                        if specified, any arguments to the
                        command may be specified after a \"--\"
  -o, --out OUT         writes all io to this file;
                        \"-\" means stdout
  -l, --line LINE       use a tty line instead of creating a
                        new pty; LINE is expected to be the
                        device
  -h, --help            show help
  -b, --bench           run config and exit
)"
#if !defined(RWTE_NO_WAYLAND) && !defined(RWTE_NO_XCB)
R"(  -x, --wayland         use wayland rather than xcb
)"
#endif
R"(  -v, --version         show version and exit
)");

static void exit_version()
{
    fmt::print("rwte {}\n", version_string());
    std::exit(EXIT_SUCCESS);
}

static bool parse_geometry(std::string_view g, int* cols, int* rows)
{
    // parse cols
    char* end = nullptr;
    // todo: std::from_chars
    // hack: we just happen to know that g has (todo: fix)
    int c = strtol(g.data(), &end, 10);
    if (c > 0 && *end == 'x') {
        // move past x
        end++;

        // parse rows
        // todo: std::from_chars
        int r = strtol(end, &end, 10);
        if (r > 0 && *end == 0) {
            *cols = c;
            *rows = r;
            return true;
        }
    }

    return false;
}

int main(int argc, char* argv[])
{
    auto bus = std::make_shared<event::Bus>();
    rwte = std::make_unique<Rwte>(bus);
    auto L = rwte->lua();

    // register internal modules, logging first
    register_lualogging(L.get());
    register_luaterm(L.get());
    register_luawindow(L.get());

    // feed lua our args
    L->newtable();
    for (int i = 0; i < argc; i++) {
        L->pushstring(argv[i]);
        L->seti(-2, i + 1);
    }
    L->setglobal("args");

    std::string_view confpath;
    std::string_view geometry;
    std::string_view exec;
    bool show_version = false;

    int cols = 0, rows = 0;
    bool got_bench = false;

#if !defined(RWTE_NO_WAYLAND) && !defined(RWTE_NO_XCB)
    bool got_wayland = false;
#endif

    auto p = rw::argparse::parser{}
        .optional(&confpath, "config"sv, "c"sv)
        .optional(&options.winclass, "winclass"sv, "w"sv)
        .optional(&options.noalt, "noalt"sv, "a"sv)
        .optional(&options.font, "font"sv, "f"sv)
        .optional(&geometry, "geometry"sv, "g"sv) // colsxrows, e.g., 80x24
        .optional(&options.title, "title"sv, "t"sv)
        .optional(&options.winname, "name"sv, "n"sv)
        .optional(&exec, "exe"sv, "e"sv) // todo: rename exec?
        .optional(&options.io, "out"sv, "o"sv)
        .optional(&options.line, "line"sv, "l"sv)
        .optional(&got_bench, "bench"sv, "b"sv)
#if !defined(RWTE_NO_WAYLAND) && !defined(RWTE_NO_XCB)
        .optional(&got_wayland, "wayland"sv, "x"sv)
#endif
        .optional(&show_version, "version"sv, "v"sv)
        .usage(usage);
        rw::logging::dbg()->info("parser 8 {})",
                (uint64_t)((void*)&p));
    if (!p.parse(argc, argv)) {
        return EXIT_FAILURE;
    }

    // todo: capture exec args!
    // capture args with we had -e
    // if (got_exe)
    //     options.cmd.push_back(argv[optind]);

    if (!geometry.empty()) {
        if (!parse_geometry(geometry, &cols, &rows))
            LOGGER()->warn("ignoring invalid geometry '{}'", geometry);
    }

    if (show_version) {
        exit_version();
    }

    if (!exec.empty()) {
        LOGGER()->info("exec: '{}'", exec);
        // todo: we know that exec ends with a \0 ...this is cheating?
        options.cmd.push_back(exec.data());
    }

#if !defined(RWTE_NO_WAYLAND) && !defined(RWTE_NO_XCB)
    if (got_wayland) {
        LOGGER()->debug("using wayland", optarg);
    }
#endif

    {
        // Get XDG basedir data
        xdgHandle xdg;
        xdgInitHandle(&xdg);

        // make a list of pachage search paths
        std::vector<std::string> searchpaths;
        const char* const* xdgconfigdirs = xdgSearchableConfigDirectories(&xdg);
        for (; *xdgconfigdirs; xdgconfigdirs++) {
            // append /rwte to each dir
            std::string path = *xdgconfigdirs;
            path += "/rwte";
            searchpaths.push_back(path);
        }

        // add search paths to lua world
        L->getglobal("package");
        if (L->istable(-1)) {
            L->getfield(-1, "path");
            add_to_search_path(L.get(), searchpaths, true);
            L->setfield(-2, "path");

            L->getfield(-1, "cpath");
            add_to_search_path(L.get(), searchpaths, false);
            L->setfield(-2, "cpath");
        } else
            LOGGER()->error("package is not a table");
        L->pop();

        // find and run configuration file
        // todo: we know that confpath ends with a \0 ...this is cheating?
        if (!run_config(L.get(), &xdg, confpath.data()))
            LOGGER()->fatal("could not find/run config.lua");

        xdgWipeHandle(&xdg);
    }

    // nothing else to do if bench arg was specified
    if (got_bench)
        return 0;

    // if a title was passed on command line, then
    // use that rather than checking lua config
    if (options.title.empty()) {
        L->getglobal("config");
        if (L->istable(-1)) {
            L->getfield(-1, "title");
            auto title = L->tostring(-1);
            if (!title.empty())
                options.title = title;
            L->pop();
        } else
            LOGGER()->fatal("expected 'config' to be table");
        L->pop();
    }

    // get cols and rows, default to 80x24
    if (cols == 0 || rows == 0) {
        L->getglobal("config");
        L->getfield(-1, "default_cols");
        cols = L->tointegerdef(-1, 80);
        L->getfield(-2, "default_rows");
        rows = L->tointegerdef(-1, 24);
        L->pop(3);
    }

    cols = std::max(cols, 1);
    rows = std::max(rows, 1);

    try {
        // get ready, loop!
        ev::default_loop main_loop;

        auto term = std::make_shared<term::Term>(bus, cols, rows);
        auto tty = std::make_shared<Tty>(bus, term);
        term->setTty(tty);

        std::shared_ptr<Window> window;

#if defined(RWTE_NO_WAYLAND)
        window = createXcbWindow(bus, term, tty);
#elif defined(RWTE_NO_XCB)
        options.throttledraw = false;
        window = createWlWindow(bus, term, tty);
#else
        if (!got_wayland)
            window = createXcbWindow(bus, term, tty);
        else {
            options.throttledraw = false;
            window = createWlWindow(bus, term, tty);
        }
#endif

        rwte->setWindow(window);
        rwte->setTerm(term);
        term->setWindow(window);
        lua::setTerm(L.get(), term);
        lua::window::setWindow(L.get(), window);

        tty->open(window.get());

        {
            SigWatcher sigwatcher;
            main_loop.run();
        }
    } catch (const WindowError& e) {
        LOGGER()->error(fmt::format("window error: {}", e.what()));
    }

    LOGGER()->debug("exiting");
    return 0;
}
