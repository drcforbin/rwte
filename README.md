# rwte

xcb terminal emulator, with lua, cairo, and pango

## Todo

* Mouse click / drag to select in vim is broken
* Shifted function/insert/del/end/etc keys, like what's done in urxvt
* Parse command arguments
* Implement urgent and bell in Window
* Look into XEMBED
* Straighten out responsibilities in copy/pasting, and pick a better
  data type for passing around the data
* Numlock. I don't have a keypad.
* Custom colors, color resetting
* Media copy / 'print'

## Notes

Borrows code and ideas from all over
* most code has been derived from st and rxvt-unicode, but xterm and vte
  were involved too. This terminal has no real advantage over the others;
  it's a toy project.
* the excellent [fmt](https://github.com/fmtlib/fmt) library is embedded
