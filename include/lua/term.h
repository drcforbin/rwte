#ifndef LUA_TERM_H
#define LUA_TERM_H

// lua term integration

namespace lua
{

class State;

void register_luaterm(State *L);

} // namespace lua

#endif // LUA_TERM_H
