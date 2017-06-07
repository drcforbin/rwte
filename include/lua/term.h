#ifndef LUATERM_H
#define LUATERM_H

// lua term integration

namespace lua
{

class State;

void register_luaterm(State *L);

} // namespace lua

#endif // LUATERM_H
