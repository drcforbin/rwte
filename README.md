# rwte

xcb terminal emulator, with lua, cairo, and pango

## Todo

* Shifted function/insert/del/end/etc keys, like what's done in urxvt
* Implement urgent and bell in Window
* Look into XEMBED
* Straighten out responsibilities in copy/pasting, and pick a better
  data type for passing around the data
* Numlock. I don't have a keypad.
* Custom colors, color resetting
* Media copy / 'print'
* -e argument

## Notes

Borrows code and ideas from all over
* most code has been derived from st and rxvt-unicode, but xterm and vte
  were involved too. This terminal has no real advantage over the others;
  it's a toy project.
* the excellent [fmt](https://github.com/fmtlib/fmt) library is embedded

## Lua API

* [Introduction](#introduction)
* [config](#config)
* [term](#term)
* [window](#window)
* [logging](#logging)

### Introduction

At startup, a config file may be specified using the `-c` or `--config`, or rwte
will search the search for a `config.lua` using xdg paths (`~/.config/rwte` is
the most convenient place to put it). This file will be loaded and run in the
global context after loading the normal lua libs and the `term`, `window`, and
`logging` modules described below (note that the terminal and window aren't yet
created, but handlers may be registered).

If the `-b` of `--bench` argument is supplied, rwte will exit after the config
file is loaded/run. Lua code can look for these arguments to run tests and
benchmarking.

The config file is expected to assign to set a global `config` object, and may
also connect event handlers for `window` / `term` events.

### config

`config` is a global lua table, assigned in the config file. Values in `config`
may be overridden by command line args to rwte. The expected contents and
possible values for fields in `config` are documented by comments in the example
`config.lua`.

### term

`term` represents the global terminal object.

#### term.mode

**syntax:** *val = term.mode(mode)*

Returns whether a terminal mode is set.

Example:
```lua
 is_crlf = term.mode(term.modes.MODE_CRLF)
```

#### term.modes

**syntax:** *val = term.modes.TERM_xxx*

Mode flags for used with `term.mode`

*todo: document term.modes.MODE_xxx*

#### term.send

**syntax:** *term.send(s)*

Sends a string to the terminal.

Example:
```lua
 term.send("\025")
```

#### term.clipcopy

**syntax:** *term.clipcopy()*

Initiates copy of the terminal selection to the system clipboard.

Example:
```lua
 term.clipcopy()
```

### window

`window` represents the global window object.

#### window.mouse_press

**syntax:** *window.mouse_press(func)*

Sets the function to be called when a mouse button is pressed (except
when the mouse button is handled internally, such as for selection).

The function passed to `mouse_press` will be called with arguments as follows:

| name     | desc |
| -------- | ---- |
| `col`    | Terminal column where the event occurred |
| `row`    | Terminal row where the event occurred |
| `button` | Mouse button (integer, 1-5) |
| `mod`    | Keyboard modifiers; this is a table containing `shift`, `ctrl`, `alt` and `logo` bool fields. |

The function should return true if the mouse press was handled, something
falsy otherwise.

Example:
```lua
 window.mouse_press(function(col, row, button, mod)
     if button == 4 then -- mouse wheel up
         logging.get("example"):info("WHEEEEEL UP!")
         return true
     end
 end)
```

#### window.key_press

**syntax:** *window.key_press(func)*

Sets the function to be called when a key is pressed (except when the key
button is handled internally).

The function passed to `key_press` will be called with arguments as follows:

| name  | desc |
| ----- | ---- |
| `sym` | Key symbol. See values in `window.keys`. |
| `mod` | Keyboard modifiers; this is a table containing `shift`, `ctrl`, `alt` and `logo` bool fields. |

The function should return true if the key press was handled, something
falsy otherwise.

Example:
```lua
 window.key_press(function(sym, mod)
     -- check for special commands
     if mod.shift then
         if sym == window.keys.Q then
             logging.get("example"):info("Q!!!")
             return true
         elseif sym == window.keys.W then
             logging.get("example"):info("W!!")
             return true
         end
     end
 end)
```

#### window.clippaste

**syntax:** *window.clippaste()*

Initiates paste of the system clipboard to the terminal.

Example:
```lua
 window.clippaste()
```

#### window.selpaste

**syntax:** *window.selpaste()*

Initiates paste of the system selection to the terminal.

Example:
```lua
 window.selpaste()
```

#### window.keys

**syntax:** *val = window.keys.xxx*

Key symbols for use in `window.key_press` handler.

*todo: document window.keys.xxx*

#### window.id

**syntax:** *val = window.id*

Returns current window id as an integer.

Example:
```lua
 id = window.id
```

### logging

*todo: document logging*

