#ifndef RWTE_CONFIG_H
#define RWTE_CONFIG_H

// masks to force mouse select, can be EMPTY_MASK to disable
// IN CONFIG, NOT READING YET
#define FORCE_SEL_MOD SHIFT_MASK

// todo: move or remove
// old stuff
// X(TERMMOD,              XK_Prior,       zoom,           {.f = +1}) \
// X(TERMMOD,              XK_Next,        zoom,           {.f = -1}) \
// X(TERMMOD,              XK_Home,        zoomreset,      {.f =  0}) \
// X(ANY_MOD,           XK_Break,       sendbreak,      {.i =  0})
// X(ControlMask,          XK_Print,       toggleprinter,  {.i =  0))
// X(ShiftMask,            XK_Print,       printscreen,    {.i =  0})
// X(ANY_MOD,           XK_Print,       printsel,       {.i =  0} })
// X(TERMMOD,              XK_Num_Lock,    numlock,        {.i =  0})
// X(TERMMOD,              XK_I,           iso14755,       {.i =  0})

#define CONFIG_FILE "~/.rwte.lua"
#define RWTE_LIB_PATH "/usr/share/rwte/lib"

#endif // RWTE_CONFIG_H
