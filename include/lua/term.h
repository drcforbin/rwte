#ifndef LUA_TERM_H
#define LUA_TERM_H

#include <memory>

namespace term {
class Term;
}

// lua term integration

namespace lua {

class State;

void setTerm(State* L, std::shared_ptr<term::Term> term);

void register_luaterm(State* L);

} // namespace lua

#endif // LUA_TERM_H
