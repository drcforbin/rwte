#ifndef LUA_STATE_H
#define LUA_STATE_H

#include <lua.hpp>
#include <string>

namespace lua {

class State
{
public:
    State();
    State(lua_State* L);
    ~State();

    lua_State* state() const { return m_L; }

    void openlibs();
    int loadfile(const char* filename);
    void call(int nargs, int nresults);
    int pcall(int nargs, int nresults, int msgh);
    void pop(int n = 1);
    void remove(int index);
    void pushvalue(int index);
    int gettop();

    void newlib(const luaL_Reg* l, int entries = 0);
    void setfuncs(const luaL_Reg* l, int nup = 0);
    void requiref(const char* modname, lua_CFunction openf, bool glb);
    void pushcfunction(lua_CFunction f);

    void pushnil();
    void pushlightuserdata(void* p);
    void* touserdata(int index);
    void pushcclosure(int (*fn)(lua_State*), int n);
    static int upvalueindex(int i);

    void newtable();
    void setmetatable(int index);
    bool getmetatable(int index);

    bool newmetatable(const char* tname);
    void setmetatable(const char* tname);
    void getmetatable(const char* tname);

    int getglobal(const char* name);
    void setglobal(const char* name);

    void concat(int n);

    int type(int index);
    bool isnil(int index);
    bool istable(int index);
    int getfield(int index, const char* k);
    void setfield(int index, const char* k);
    int geti(int index, int i);
    void seti(int index, int n);

    const char* tostring(int index);
    const char* tolstring(int index, size_t* len);
    const char* checkstring(int arg);
    const char* checklstring(int arg, size_t* l);
    void pushstring(const char* s);
    void pushstring(const std::string& s);

    int tointeger(int index);
    int tointegerx(int index, int* isnum);
    int tointegerdef(int index, int def);
    int checkinteger(int arg);
    void pushinteger(int n);

    float tonumber(int index);
    float tonumberx(int index, int* isnum);
    float tonumberdef(int index, float def);

    bool tobool(int index);
    bool tobooldef(int index, bool def);
    void pushbool(bool b);

    void setobjfuncs(const char* tname, const luaL_Reg* funcs);

    template <typename T>
    T* newobj(const char* tname = nullptr)
    {
        auto buf = lua_newuserdata(m_L, sizeof(T));
        auto p = new (buf) T();
        if (tname)
            setmetatable(tname);
        return p;
    }

    template <typename T>
    T* checkobj(int arg, const char* tname)
    {
        return static_cast<T*>(
                luaL_checkudata(m_L, arg, tname));
    }

    template <typename T>
    void delobj(int arg, const char* tname)
    {
        checkobj<T>(arg, tname)->~T();
    }

    // stores a reference to the value at arg on
    // of the stack in the global registry and returns
    // the index value. if oldref is not LUA_NOREF, it
    // will be released before the new value is stored.
    // the value at arg must be a func or nil.
    int setref(int arg, int oldref = LUA_NOREF);

    // retrieves a referenced value. returns true
    // if the value was retrieved, false if not or
    // if it was nil (leaving the stack unchanged)
    bool pushref(int ref);

    // stores a reference to the function at arg on
    // of the stack in the global registry and returns
    // the index value. if oldref is not LUA_NOREF, it
    // will be released before the new value is stored.
    // the value at arg must be a func or nil.
    int setfuncref(int arg, int oldref = LUA_NOREF);

private:
    lua_State* m_L = nullptr;
    bool m_owns = false;
};

} // namespace lua

#endif // LUA_STATE_H
