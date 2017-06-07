#ifndef LUALOGGING_H
#define LUALOGGING_H

// lua log integration

namespace lua
{

class State;

void register_lualogging(State *L);

} // namespace lua

#endif // LUALOGGING_H
