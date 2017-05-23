#include "rwte/luastate.h"
#include "rwte/lualogging.h"
#include "rwte/logging.h"

#define LUALOG "LUALOG*"

struct LuaLogStruct
{
    std::shared_ptr<logging::Logger> logger;
};

static inline std::shared_ptr<logging::Logger> tologger(LuaState& L)
{
    auto p = L.checkobj<LuaLogStruct>(1, LUALOG);
    return p->logger;
}

static int logger_log(lua_State *l)
{
    LuaState L(l);
    auto logger = tologger(L);

    auto level = (logging::level_enum) L.checkinteger(2);
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
    LuaState L(l);
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

LOGGER_FUNC(trace)
LOGGER_FUNC(debug)
LOGGER_FUNC(info)
LOGGER_FUNC(warn)
LOGGER_FUNC(err)
LOGGER_FUNC(fatal)

static int logger_level(lua_State *l)
{
    LuaState L(l);
    auto logger = tologger(L);

    // if we got a second arg, try to set level
    if (L.gettop() > 1)
    {
        auto level = (logging::level_enum) L.checkinteger(2);
        logger->level(level);
    }

    L.pushinteger(logger->level());
    return 1;
}

static int logger_gc(lua_State *l)
{
    LuaState(l).delobj<LuaLogStruct>(1, LUALOG);
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
    {"level", logger_level},
    {"__gc", logger_gc},
    {NULL, NULL},
};

static int logging_get(lua_State *l)
{
    // get the name before doing any allocation
    LuaState L(l);
    const char *name = L.checkstring(1);

    // alloc and init
    auto p = L.newobj<LuaLogStruct>(LUALOG);
    p->logger = logging::get(name);
    return 1;
}

// functions for logging library
static const luaL_Reg logging_funcs[] = {
    {"get", logging_get},
    {NULL, NULL}
};

static int logging_openf(lua_State *l)
{
    LuaState L(l);

    // make the lib (1 func, 7 values)
    L.newlib(logging_funcs, 8);

    // add log levels
    L.pushinteger(logging::trace);
    L.setfield(-2, "trace");
    L.pushinteger(logging::debug);
    L.setfield(-2, "debug");
    L.pushinteger(logging::info);
    L.setfield(-2, "info");
    L.pushinteger(logging::warn);
    L.setfield(-2, "warn");
    L.pushinteger(logging::err);
    L.setfield(-2, "err");
    L.pushinteger(logging::fatal);
    L.setfield(-2, "fatal");
    L.pushinteger(logging::off);
    L.setfield(-2, "off");

    // add LUALOG object
    L.setobjfuncs(LUALOG, logger_funcs);

	return 1;
}

void register_lualogging(LuaState *L)
{
    L->requiref("logging", logging_openf, true);
    L->pop();
}

