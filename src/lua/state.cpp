#include "lua/state.h"

namespace lua
{

State::State() :
    m_L(luaL_newstate()),
    m_owns(true)
{ }

State::State(lua_State *L) :
    m_L(L),
    m_owns(false)
{ }

State::~State()
{
    if (m_owns)
        lua_close(m_L);
}

void State::openlibs()
{ luaL_openlibs(m_L); }

int State::loadfile(const char *filename)
{ return luaL_loadfile(m_L, filename); }

void State::call(int nargs, int nresults)
{ lua_call(m_L, nargs, nresults); }

int State::pcall(int nargs, int nresults, int msgh)
{ return lua_pcall(m_L, nargs, nresults, msgh); }

void State::pop(int n /* = 1 */)
{ lua_pop(m_L, n); }

void State::remove(int index)
{ lua_remove(m_L, index); }

void State::pushvalue(int index)
{ lua_pushvalue(m_L, index); }

int State::gettop()
{ return lua_gettop(m_L); }

void State::newlib(const luaL_Reg *l, int entries /* = 0 */)
{
    // if entries was not specified, assume the library
    // is nothing but functions, and they're all in l
    if (!entries)
    {
        // count to sentinel
        int entries = 0;
        while (l[entries].name)
            entries++;
    }

    lua_createtable(m_L, 0, entries);
    luaL_setfuncs(m_L, l, 0);
}

void State::setfuncs(const luaL_Reg *l, int nup /* = 0 */)
{ luaL_setfuncs(m_L, l, nup); }

void State::requiref(const char *modname, lua_CFunction openf, bool glb)
{ luaL_requiref(m_L, modname, openf, glb? 1 : 0); }

void State::pushcfunction(lua_CFunction f)
{ lua_pushcfunction(m_L, f); }

void State::pushnil()
{ lua_pushnil(m_L); }

void State::pushlightuserdata(void *p)
{ lua_pushlightuserdata(m_L, p); }

void *State::touserdata(int index)
{ return lua_touserdata(m_L, index); }

void State::pushcclosure(int (*fn)(lua_State *), int n)
{ lua_pushcclosure(m_L, fn, n); }

int State::upvalueindex(int i)
{ return lua_upvalueindex(i); }

void State::newtable()
{ lua_newtable(m_L); }

void State::setmetatable(int index)
{ lua_setmetatable(m_L, index); }

bool State::newmetatable(const char *tname)
{ return luaL_newmetatable(m_L, tname) != 0; }

void State::setmetatable(const char *tname)
{ luaL_setmetatable(m_L, tname); }

int State::getglobal(const char *name)
{ return lua_getglobal(m_L, name); }

void State::setglobal(const char *name)
{ lua_setglobal(m_L, name); }

void State::concat(int n)
{ lua_concat(m_L, n); }

int State::type(int index)
{ return lua_type(m_L, index); }

bool State::isnil(int index)
{ return lua_isnil(m_L, index) != 0; }

bool State::istable(int index)
{ return lua_istable(m_L, index) != 0; }

int State::getfield(int index, const char *k)
{ return lua_getfield(m_L, index, k); }

void State::setfield(int index, const char *k)
{ return lua_setfield(m_L, index, k); }

int State::geti(int index, int i)
{ return lua_geti(m_L, index, i); }

void State::seti(int index, int n)
{ lua_seti(m_L, index, n); }

const char *State::tostring(int index)
{ return lua_tostring(m_L, index); }

const char *State::tolstring(int index, size_t *len)
{ return lua_tolstring(m_L, index, len); }

const char *State::checkstring(int arg)
{ return luaL_checkstring(m_L, arg); }

const char *State::checklstring(int arg, size_t *l)
{ return luaL_checklstring(m_L, arg, l); }

void State::pushstring(const char *s)
{ lua_pushstring(m_L, s); }

void State::pushstring(const std::string& s)
{ lua_pushlstring(m_L, s.c_str(), s.size()); }

int State::tointeger(int index)
{ return lua_tointeger(m_L, index); }

int State::tointegerx(int index, int *isnum)
{ return lua_tointegerx(m_L, index, isnum); }

int State::tointegerdef(int index, int def)
{
    int isnum = 0;
    int val = tointegerx(index, &isnum);
    if (isnum)
        return val;
    else
        return def;
}

int State::checkinteger(int arg)
{ return luaL_checkinteger(m_L, arg); }

void State::pushinteger(int n)
{ lua_pushinteger(m_L, n); }

float State::tonumber(int index)
{ return lua_tonumber(m_L, index); }

float State::tonumberx(int index, int *isnum)
{ return lua_tonumberx(m_L, index, isnum); }

float State::tonumberdef(int index, float def)
{
    int isnum = 0;
    float val = tonumberx(index, &isnum);
    if (isnum)
        return val;
    else
        return def;
}

bool State::tobool(int index)
{ return lua_toboolean(m_L, index) != 0; }

bool State::tobooldef(int index, bool def)
{
    if (isnil(index))
        return def;
    else
        return tobool(index);
}

void State::pushbool(bool b)
{ lua_pushboolean(m_L, b? 1 : 0); }

void State::setobjfuncs(const char *tname, const luaL_Reg *funcs)
{
    newmetatable(tname); // create metatable for obj
    pushvalue(-1); // push metatable
    setfield(-2, "__index");  // metatable.__index = metatable
    setfuncs(funcs); // add methods to new metatable
    pop(); // pop new metatable
}

int State::setfuncref(int arg, int oldref /* = LUA_NOREF */)
{
    luaL_argcheck(m_L, lua_isnil(m_L, arg) || lua_isfunction(m_L, arg),
            arg, "should be nil or a function");

    if (oldref != LUA_NOREF)
        luaL_unref(m_L, LUA_REGISTRYINDEX, oldref);

    pushvalue(arg);
    return luaL_ref(m_L, LUA_REGISTRYINDEX);
}

bool State::pushfuncref(int ref)
{
    if (ref != LUA_NOREF && ref != LUA_REFNIL)
    {
        lua_rawgeti(m_L, LUA_REGISTRYINDEX, ref);
        if (!isnil(-1) )
            return true;
        pop();
    }

    return false;
}

} // namespace lua
