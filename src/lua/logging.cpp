#include "lua/logging.h"
#include "lua/state.h"
#include "rwte/logging.h"

/// Logging module; this is the api for logging.
// @module logging

/// Logger class.
// @type Logger

#define LUALOG "LUALOG*"

struct LuaLogStruct
{
    std::shared_ptr<logging::Logger> logger;
};

static inline std::shared_ptr<logging::Logger> tologger(lua::State& L)
{
    auto p = L.checkobj<LuaLogStruct>(1, LUALOG);
    return p->logger;
}

/// Writes log entry with specified level.
// @function log
// @int level Log level
// @param ... Values to include in the entry (`tostring` will be called for each)
static int logger_log(lua_State *l)
{
    lua::State L(l);
    auto logger = tologger(L);

    auto level = static_cast<logging::level_enum>(L.checkinteger(2));
    if (level < logger->level())
        return 0;

    // count args
    int nargs = L.gettop();

    L.getglobal("tostring");

    std::string msg;

    size_t len;
    for (int i = 3; i <= nargs; i++)
    {
        L.pushvalue(-1); // push tostring
        L.pushvalue(i); // push next arg
        L.call(1, 1);

        const char *s = L.tolstring(-1, &len);
        if (!s)
            return luaL_error(l, "tostring must return a string to print");

        msg += s;
        if (i > 1)
            msg += "\t";

        L.pop();
    }

    L.pop(); // pop tostring

    logger->log(level, msg.c_str());
    return 0;
}

static void call_log(lua_State *l, logging::level_enum level)
{
    lua::State L(l);
    auto logger = tologger(L);

    if (level < logger->level())
        return;

    // count args
    int nargs = L.gettop();

    // get the log func, push self and level
    L.getfield(1, "log");
    L.pushvalue(1);
    L.pushinteger(level);

    // push non-self args
    for (int i = 2; i <= nargs; i++)
        L.pushvalue(i);

    // call, with one more arg (level)
    L.call(1 + nargs, 0);
}

#define LOGGER_FUNC(levelname) \
    static int logger_##levelname(lua_State *L) \
    { call_log(L, logging::levelname); return 0; }

/// Writes trace level log entry.
// @function trace
// @param ... Values to include in the entry (`tostring` will be called for each)
LOGGER_FUNC(trace)
/// Writes debug level log entry.
// @function debug
// @param ... Values to include in the entry (`tostring` will be called for each)
LOGGER_FUNC(debug)
/// Writes info level log entry.
// @function info
// @param ... Values to include in the entry (`tostring` will be called for each)
LOGGER_FUNC(info)
/// Writes warn level log entry.
// @function warn
// @param ... Values to include in the entry (`tostring` will be called for each)
LOGGER_FUNC(warn)
/// Writes error level log entry.
// @function err
// @param ... Values to include in the entry (`tostring` will be called for each)
LOGGER_FUNC(err)
/// Writes fatal level log entry.
// @function fatal
// @param ... Values to include in the entry (`tostring` will be called for each)
LOGGER_FUNC(fatal)

/// Log level for this logger.
//
// (implemented via `__index` / `__newindex`)
//
// @class field
// @name level

static int logger_index(lua_State *l)
{
    lua::State L(l);
    auto logger = tologger(L);

    const char * key = L.checkstring(2);

    if (std::strcmp(key, "level") == 0)
        L.pushinteger(logger->level());
    else
        L.pushnil();

    return 1;
}

static int logger_newindex(lua_State *l)
{
    lua::State L(l);
    auto logger = tologger(L);

    const char * key = L.checkstring(2);

    if (std::strcmp(key, "level") == 0)
    {
        auto level = static_cast<logging::level_enum>(L.checkinteger(3));
        logger->level(level);
    }

    return 0;
}

static int logger_gc(lua_State *l)
{
    lua::State(l).delobj<LuaLogStruct>(1, LUALOG);
    return 0;
}

// methods for logger object
static const luaL_Reg logger_funcs[] = {
    {"log", logger_log},
    {"trace", logger_trace},
    {"debug", logger_debug},
    {"info", logger_info},
    {"warn", logger_warn},
    {"err", logger_err},
    {"fatal", logger_fatal},
    {"__index", logger_index},
    {"__newindex", logger_newindex},
    {"__gc", logger_gc},
    {nullptr, nullptr},
};

/// Functions
// @section functions

/// Retrieves a named @{Logger} instance.
//
// @function logging.get
// @string nm Name of the logger to create/retrieve.
// @usage logger = logging.get("config")
static int logging_get(lua_State *l)
{
    // get the name before doing any allocation
    lua::State L(l);
    const char *name = L.checkstring(1);

    // alloc and init
    auto p = L.newobj<LuaLogStruct>(LUALOG);
    p->logger = logging::get(name);
    return 1;
}

// functions for logging library
static const luaL_Reg logging_funcs[] = {
    {"get", logging_get},
    {nullptr, nullptr}
};

static int logging_openf(lua_State *l)
{
    lua::State L(l);

    // make the lib (1 func, 7 values)
    L.newlib(logging_funcs, 8);

    /// Fields
    // @section fields

    /// Trace level
    // @class field
    // @name trace
    L.pushinteger(logging::trace);
    L.setfield(-2, "trace");
    /// Debug level
    // @class field
    // @name debug
    L.pushinteger(logging::debug);
    L.setfield(-2, "debug");
    /// Info level
    // @class field
    // @name info
    L.pushinteger(logging::info);
    L.setfield(-2, "info");
    /// Warn level
    // @class field
    // @name warn
    L.pushinteger(logging::warn);
    L.setfield(-2, "warn");
    /// Error level
    // @class field
    // @name err
    L.pushinteger(logging::err);
    L.setfield(-2, "err");
    /// Fatal level
    // @class field
    // @name fatal
    L.pushinteger(logging::fatal);
    L.setfield(-2, "fatal");
    /// Logging off
    // @class field
    // @name off
    L.pushinteger(logging::off);
    L.setfield(-2, "off");

    // add LUALOG object
    L.setobjfuncs(LUALOG, logger_funcs);

    return 1;
}

void lua::register_lualogging(lua::State *L)
{
    L->requiref("logging", logging_openf, true);
    L->pop();
}

