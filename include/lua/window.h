#ifndef LUAWINDOW_H
#define LUAWINDOW_H

// ugh. hate to have to include this just for keymod_state!
#include "rwte/term.h"

// lua window integration

namespace lua
{

class State;

void register_luawindow(State *L);

namespace window
{

bool call_mouse_press(State *L, int col, int row, int button,
        const keymod_state& mods);
bool call_key_press(State *L, int keysym,
        const keymod_state& mods);

} // namespace window
} // namespace lua

#endif // LUAWINDOW_H
