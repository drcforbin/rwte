#include "rwte/rwte.h"
#include "rwte/luastate.h"
#include "rwte/luaconfig.h"

int luaconfig::get_int(const char *name, int def)
{
    auto L = rwte.lua();
    L->getglobal("config");
    L->getfield(-1, name);
    int val = L->tointegerdef(-1, def);
    L->pop(2);

    return val;
}

float luaconfig::get_float(const char *name, float def)
{
    auto L = rwte.lua();
    L->getglobal("config");
    L->getfield(-1, name);
    float val = L->tonumberdef(-1, def);
    L->pop(2);

    return val;
}

bool luaconfig::get_bool(const char *name, bool def)
{
    auto L = rwte.lua();
    L->getglobal("config");
    L->getfield(-1, name);
    float val = L->tobooldef(-1, def);
    L->pop(2);

    return val;
}

std::string luaconfig::get_string(const char *name)
{
    std::string val;

    auto L = rwte.lua();
    L->getglobal("config");
    L->getfield(-1, name);
    const char * s = L->tostring(-1);
    if (s)
        val = s;
    L->pop(2);

    return val;
}
