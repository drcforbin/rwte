#ifndef LUA_WINDOW_H
#define LUA_WINDOW_H

// ugh. hate to have to include this just for keymod_state!
#include "rwte/term.h"

// lua window integration

struct Cell;

namespace lua
{

class State;

void register_luawindow(State *L);

namespace window
{

bool call_mouse_press(State *L, const Cell& cell, int button,
        const term::keymod_state& mods);
bool call_key_press(State *L, int keysym,
        const term::keymod_state& mods);

} // namespace window
} // namespace lua

#endif // LUA_WINDOW_H
