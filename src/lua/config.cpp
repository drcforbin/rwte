#include "lua/config.h"
#include "lua/state.h"
#include "rwte/rwte.h"

namespace lua {
namespace config {

int get_int(const char* name, int def)
{
    auto L = rwte->lua();
    L->getglobal("config");
    L->getfield(-1, name);
    int val = L->tointegerdef(-1, def);
    L->pop(2);

    return val;
}

float get_float(const char* name, float def)
{
    auto L = rwte->lua();
    L->getglobal("config");
    L->getfield(-1, name);
    float val = L->tonumberdef(-1, def);
    L->pop(2);

    return val;
}

bool get_bool(const char* name, bool def)
{
    auto L = rwte->lua();
    L->getglobal("config");
    L->getfield(-1, name);
    bool val = L->tobooldef(-1, def);
    L->pop(2);

    return val;
}

std::string get_string(const char* name)
{
    auto L = rwte->lua();
    L->getglobal("config");
    L->getfield(-1, name);
    std::string val{L->tostring(-1)};
    L->pop(2);

    return val;
}

} // namespace config
} // namespace lua
