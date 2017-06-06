#ifndef LUAWINDOW_H
#define LUAWINDOW_H

// ugh. hate to have to include this just for keymod_state!
#include "rwte/term.h"

// lua window integration

class LuaState;

void register_luawindow(LuaState *L);

bool luawindow_mouse_press(LuaState *L, int col, int row, int button,
        const keymod_state& mods);
bool luawindow_key_press(LuaState *L, int keysym,
        const keymod_state& mods);

#endif // LUAWINDOW_H
