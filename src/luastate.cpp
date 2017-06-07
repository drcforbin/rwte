#include "rwte/luastate.h"

LuaState::LuaState() :
    m_L(luaL_newstate()),
    m_owns(true)
{ }

LuaState::LuaState(lua_State *L) :
    m_L(L),
    m_owns(false)
{ }

LuaState::~LuaState()
{
    if (m_owns)
        lua_close(m_L);
}

void LuaState::openlibs()
{ luaL_openlibs(m_L); }

int LuaState::loadfile(const char *filename)
{ return luaL_loadfile(m_L, filename); }

void LuaState::call(int nargs, int nresults)
{ lua_call(m_L, nargs, nresults); }

int LuaState::pcall(int nargs, int nresults, int msgh)
{ return lua_pcall(m_L, nargs, nresults, msgh); }

void LuaState::pop(int n /* = 1 */)
{ lua_pop(m_L, n); }

void LuaState::remove(int index)
{ lua_remove(m_L, index); }

void LuaState::pushvalue(int index)
{ lua_pushvalue(m_L, index); }

int LuaState::gettop()
{ return lua_gettop(m_L); }

void LuaState::newlib(const luaL_Reg *l, int entries /* = 0 */)
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

void LuaState::setfuncs(const luaL_Reg *l, int nup /* = 0 */)
{ luaL_setfuncs(m_L, l, nup); }

void LuaState::requiref(const char *modname, lua_CFunction openf, bool glb)
{ luaL_requiref(m_L, modname, openf, glb? 1 : 0); }

void LuaState::pushlightuserdata(void *p)
{ lua_pushlightuserdata(m_L, p); }

void *LuaState::touserdata(int index)
{ return lua_touserdata(m_L, index); }

void LuaState::pushcclosure(int (*fn)(lua_State *), int n)
{ lua_pushcclosure(m_L, fn, n); }

int LuaState::upvalueindex(int i)
{ return lua_upvalueindex(i); }

void LuaState::newtable()
{ lua_newtable(m_L); }

void LuaState::setmetatable(int index)
{ lua_setmetatable(m_L, index); }

bool LuaState::newmetatable(const char *tname)
{ return luaL_newmetatable(m_L, tname) != 0; }

void LuaState::setmetatable(const char *tname)
{ luaL_setmetatable(m_L, tname); }

int LuaState::getglobal(const char *name)
{ return lua_getglobal(m_L, name); }

void LuaState::setglobal(const char *name)
{ lua_setglobal(m_L, name); }

void LuaState::concat(int n)
{ lua_concat(m_L, n); }

int LuaState::type(int index)
{ return lua_type(m_L, index); }

bool LuaState::isnil(int index)
{ return lua_isnil(m_L, index) != 0; }

bool LuaState::istable(int index)
{ return lua_istable(m_L, index) != 0; }

int LuaState::getfield(int index, const char *k)
{ return lua_getfield(m_L, index, k); }

void LuaState::setfield(int index, const char *k)
{ return lua_setfield(m_L, index, k); }

int LuaState::geti(int index, int i)
{ return lua_geti(m_L, index, i); }

void LuaState::seti(int index, int n)
{ lua_seti(m_L, index, n); }

const char *LuaState::tostring(int index)
{ return lua_tostring(m_L, index); }

const char *LuaState::tolstring(int index, size_t *len)
{ return lua_tolstring(m_L, index, len); }

const char *LuaState::checkstring(int arg)
{ return luaL_checkstring(m_L, arg); }

const char *LuaState::checklstring(int arg, size_t *l)
{ return luaL_checklstring(m_L, arg, l); }

void LuaState::pushstring(const char *s)
{ lua_pushstring(m_L, s); }

void LuaState::pushstring(const std::string& s)
{ lua_pushlstring(m_L, s.c_str(), s.size()); }

int LuaState::tointeger(int index)
{ return lua_tointeger(m_L, index); }

int LuaState::tointegerx(int index, int *isnum)
{ return lua_tointegerx(m_L, index, isnum); }

int LuaState::tointegerdef(int index, int def)
{
    int isnum = 0;
    int val = tointegerx(index, &isnum);
    if (isnum)
        return val;
    else
        return def;
}

int LuaState::checkinteger(int arg)
{ return luaL_checkinteger(m_L, arg); }

void LuaState::pushinteger(int n)
{ lua_pushinteger(m_L, n); }

float LuaState::tonumber(int index)
{ return lua_tonumber(m_L, index); }

float LuaState::tonumberx(int index, int *isnum)
{ return lua_tonumberx(m_L, index, isnum); }

float LuaState::tonumberdef(int index, float def)
{
    int isnum = 0;
    float val = tonumberx(index, &isnum);
    if (isnum)
        return val;
    else
        return def;
}

bool LuaState::tobool(int index)
{ return lua_toboolean(m_L, index) != 0; }

bool LuaState::tobooldef(int index, bool def)
{
    if (isnil(index))
        return def;
    else
        return tobool(index);
}

void LuaState::pushbool(bool b)
{ lua_pushboolean(m_L, b? 1 : 0); }

void LuaState::setobjfuncs(const char *tname, const luaL_Reg *funcs)
{
    newmetatable(tname); // create metatable for obj
    pushvalue(-1); // push metatable
    setfield(-2, "__index");  // metatable.__index = metatable
    setfuncs(funcs); // add methods to new metatable
    pop(); // pop new metatable
}

int LuaState::setfuncref(int arg, int oldref /* = LUA_NOREF */)
{
    luaL_argcheck(m_L, lua_isnil(m_L, arg) || lua_isfunction(m_L, arg),
            arg, "should be nil or a function");

    if (oldref != LUA_NOREF)
        luaL_unref(m_L, LUA_REGISTRYINDEX, oldref);

    pushvalue(arg);
    return luaL_ref(m_L, LUA_REGISTRYINDEX);
}

bool LuaState::pushfuncref(int ref)
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

