#ifndef LUATERM_H
#define LUATERM_H

// ugh. hate to have to include this just for keymod_state!
#include "rwte/term.h"

// lua term integration

class LuaState;

void register_luaterm(LuaState *L);

bool luaterm_mouse_press(LuaState *L, int col, int row, int button,
        const keymod_state& mods);
bool luaterm_key_press(LuaState *L, int keysym,
        const keymod_state& mods);

#endif // LUATERM_H
