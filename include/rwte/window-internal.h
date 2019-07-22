#ifndef RWTE_WINDOW_INTERNAL_H
#define RWTE_WINDOW_INTERNAL_H

// need keymod_state
#include "term.h"

class Tty;
struct xkb_state;
struct xkb_compose_state;

void process_key(uint32_t key, term::Term *term, Tty *tty, xkb_state *state,
    xkb_compose_state *compose_state, const term::keymod_state& keymod);

#endif // RWTE_WINDOW_INTERNAL_H
