#include "rwte/luastate.h"
#include "rwte/luaterm.h"
#include "rwte/logging.h"
#include "rwte/term.h"

#define LOGGER() (logging::get("luaterm"))

static int luaterm_mode(lua_State *l)
{
    LuaState L(l);
    auto mode = (term_mode_enum) L.checkinteger(1);
    L.pushbool(g_term->mode()[mode]);
    return 1;
}

static int luaterm_send(lua_State *l)
{
    LuaState L(l);
    size_t len = 0;
    const char * buffer = L.checklstring(1, &len);
    g_term->send(buffer, len);
    return 0;
}

static int luaterm_clipcopy(lua_State *l)
{
    g_term->clipcopy();
    return 0;
}

// functions for term library
static const luaL_Reg term_funcs[] = {
    {"mode", luaterm_mode},
    {"send", luaterm_send},
    {"clipcopy", luaterm_clipcopy},
    {NULL, NULL}
};

static int term_openf(lua_State *l)
{
    LuaState L(l);

    // make the lib (3 funcs, 1 values)
    L.newlib(term_funcs, 4);

    // add modes table
    L.newtable();
#define PUSH_ENUM_FIELD(nm)\
    L.pushinteger(nm); L.setfield(-2, #nm)
    PUSH_ENUM_FIELD(MODE_WRAP);
    PUSH_ENUM_FIELD(MODE_INSERT);
    PUSH_ENUM_FIELD(MODE_APPKEYPAD);
    PUSH_ENUM_FIELD(MODE_ALTSCREEN);
    PUSH_ENUM_FIELD(MODE_CRLF);
    PUSH_ENUM_FIELD(MODE_MOUSEBTN);
    PUSH_ENUM_FIELD(MODE_MOUSEMOTION);
    PUSH_ENUM_FIELD(MODE_REVERSE);
    PUSH_ENUM_FIELD(MODE_KBDLOCK);
    PUSH_ENUM_FIELD(MODE_HIDE);
    PUSH_ENUM_FIELD(MODE_ECHO);
    PUSH_ENUM_FIELD(MODE_APPCURSOR);
    PUSH_ENUM_FIELD(MODE_MOUSESGR);
    PUSH_ENUM_FIELD(MODE_8BIT);
    PUSH_ENUM_FIELD(MODE_BLINK);
    PUSH_ENUM_FIELD(MODE_FBLINK);
    PUSH_ENUM_FIELD(MODE_FOCUS);
    PUSH_ENUM_FIELD(MODE_MOUSEX10);
    PUSH_ENUM_FIELD(MODE_MOUSEMANY);
    PUSH_ENUM_FIELD(MODE_BRCKTPASTE);
    PUSH_ENUM_FIELD(MODE_PRINT);
    PUSH_ENUM_FIELD(MODE_UTF8);
    PUSH_ENUM_FIELD(MODE_SIXEL);
#undef PUSH_ENUM_FIELD
    L.setfield(-2, "modes");

	return 1;
}

void register_luaterm(LuaState *L)
{
    L->requiref("term", term_openf, true);
    L->pop();
}
