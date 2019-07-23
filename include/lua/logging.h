#ifndef LUA_LOGGING_H
#define LUA_LOGGING_H

// lua log integration

namespace lua {

class State;

void register_lualogging(State* L);

} // namespace lua

#endif // LUA_LOGGING_H
