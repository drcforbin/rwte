#include "lua/state.h"
#include "lua/term.h"
#include "rwte/logging.h"
#include "rwte/term.h"

/// Term module; `term` is the global terminal object.
// @module term

#define LOGGER() (logging::get("luaterm"))

static int term_ref = LUA_NOREF;

const char* const LUATERM = "LUATERM*";

struct LuaTermStruct
{
    std::weak_ptr<term::Term> term;
};

// todo: merge this with lua window's getwindow, and turn it into
// a sort of generic "get shared object" func in lua::State
static inline std::shared_ptr<term::Term> getterm(lua::State& L)
{
    if (L.pushref(term_ref)) {
        // get ptr
        auto p = static_cast<LuaTermStruct*>(L.touserdata(-1));
        if (p) {
            // fetch its metatable
            if (L.getmetatable(-1)) {
                // fetch expected metatable by name, and compare them
                L.getmetatable(LUATERM);
                // todo: add rawequal to lua::State
                if (!lua_rawequal(L.state(), -1, -2))
                    p = nullptr;
                // pop both tables
                L.pop(2);
            }
        }
        // pop pushed val
        L.pop();

        if (p)
            return p->term.lock();
    }

    return {};
}

static int term_gc(lua_State* l)
{
    lua::State(l).delobj<LuaTermStruct>(1, LUATERM);
    return 0;
}

// methods for term object
static const luaL_Reg term_obj_funcs[] = {
        {"__gc", term_gc},
        {nullptr, nullptr},
};

/// Returns whether a terminal mode is set.
//
// @function mode
// @int mode Mode flag. See values in @{modes}
// @usage
// is_crlf = term.mode(term.modes.MODE_CRLF)
static int luaterm_mode(lua_State* l)
{
    lua::State L(l);
    auto mode = static_cast<term::term_mode_enum>(L.checkinteger(1));

    auto term = getterm(L);
    if (term)
        L.pushbool(term->mode()[mode]);
    else
        L.pushbool(false);

    return 1;
}

/// Sends a string to the terminal.
//
// @function send
// @string s String to send
// @usage
// term.send("\025")
static int luaterm_send(lua_State* l)
{
    lua::State L(l);
    if (auto term = getterm(L)) {
        auto buffer = L.checkstring(1);
        term->send(buffer);
    }

    return 0;
}

/// Initiates copy of the terminal selection to the system clipboard.
//
// @function clipcopy
// @usage
// term.clipcopy()
static int luaterm_clipcopy(lua_State* l)
{
    lua::State L(l);
    if (auto term = getterm(L))
        term->clipcopy();

    return 0;
}

// functions for term library
static const luaL_Reg term_funcs[] = {
        {"mode", luaterm_mode},
        {"send", luaterm_send},
        {"clipcopy", luaterm_clipcopy},
        {nullptr, nullptr}};

static int term_openf(lua_State* l)
{
    lua::State L(l);

    // make the lib (3 funcs, 1 values)
    // todo: verify that 4 is right
    L.newlib(term_funcs, 4);

    /// Mode flag table; maps mode flags to their integer value.
    // @class field
    // @name modes
    L.newtable();
#define PUSH_ENUM_FIELD(nm)  \
    L.pushinteger(term::nm); \
    L.setfield(-2, #nm)
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

    // add LUATERM object
    L.setobjfuncs(LUATERM, term_obj_funcs);

    return 1;
}

void lua::setTerm(lua::State* L, std::shared_ptr<term::Term> term)
{
    // alloc and init
    auto p = L->newobj<LuaTermStruct>(LUATERM);
    p->term = term;

    // store a ref
    term_ref = L->setref(-1, term_ref);
    L->pop();
}

void lua::register_luaterm(lua::State* L)
{
    L->requiref("term", term_openf, true);
    L->pop();
}
