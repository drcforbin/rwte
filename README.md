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

See also: [API docs](https://drcforbin.github.io/rwte/)

### config

`config` is a global lua table, assigned in the config file. Values in `config`
may be overridden by command line args to rwte. The expected contents and
possible values for fields in `config` are documented by comments in the example
`config.lua`.

### logging

*todo: document logging*

