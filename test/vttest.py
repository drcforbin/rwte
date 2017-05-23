#!/usr/bin/python

import os
import sys
import asyncio
import tty
import termios
import fcntl
import re
import curses

class Caps:
    def Caps(self):
        self.has_vt102 = False
        self.type = None

#write = sys.stdout.write
#
#def color_test():
#    text="xYz"; # Some test text
#
#    write("\n                40m   41m   42m   43m   44m   45m   46m   47m")
#
#    for fg in ["m", "1m", "30m", "1;30m", "31m", "1;31m", "32m",
#               "1;32m", "33m", "1;33m", "34m", "1;34m", "35m", "1;35m",
#               "36m", "1;36m", "37m", "1;37m"]:
#        write(" {:>5} \033[{}  {}  ".format(fg, fg, text))
#        for bg in ["40m", "41m", "42m", "43m", "44m", "45m", "46m", "47m"]:
#            write(" \033[{}\033[{} {} \033[0m".format(fg, bg, text))
#        write("\n")
#
#    write("\n")
#
#color_test()

#    yield from request_message("\033[c")

queue = asyncio.Queue()

def reader():
    while 1:
        try:
            data = sys.stdin.buffer.read(1)
            if len(data) > 0:
                asyncio.async(queue.put(data[0]))
            else:
                break
        except:
            # throws on out of data
            break

def send(cmd):
    """Sends a command; accepts either string or bytes."""

    if isinstance(cmd, str):
        cmd = cmd.encode("utf8")

    sys.stdout.buffer.write(cmd)
    sys.stdout.buffer.flush()

async def identify_test():
    # identify test
    send("\033[c")

    data = await queue.get()
    if data != 0o33:
        send("got bad byte {:x}\r\n".format(data))
        return
    else:
        caps = Caps()

        buf = ""
        while 1:
            data = await queue.get()
            ch = chr(data)
            buf += ch
            if ch == "c":
                break;

        if buf == "[?1;2c": # VT100 with Advanced Video Option
            caps.type = "VT100"
            caps.has_vt102 = False
        elif buf == "[?1;0c": # VT101 with No Options
            caps.type = "VT101"
            caps.has_vt102 = False
        elif buf == "[?6c": # VT102
            caps.type = "VT102"
            caps.has_vt102 = True
        else:
            m = re.match(r"\[\?6(\d)(?:;\d+)*c", buf)
            if m:
                # note that we don't care about the parameters
                t = m.group(1)
                if m == "2":
                    caps.type = "VT220"
                    caps.has_vt102 = True
                elif m == "3":
                    caps.type = "VT320"
                    caps.has_vt102 = True
                elif m == "4":
                    caps.type = "VT420"
                    caps.has_vt102 = True

        if caps.type:
            return caps
        else:
            send("received unexpected identify response: '{}'\r\n".format(buf))

async def cursor_command_test():
    """
    vt100 basics:
    tests moving cursor up (A), down (B), left (D), and right one step (C),
    direct moves (with H), clear line (K) and clear to bottom (J)
    """

    # go home
    send("\033[H")
    # clear screen
    send("\033[J")

    # print top line with junk
    send("cursor junkstuff\r\n")

    # draw a v (default to 1 moves)
    send("\033[B\\")
    send("\033[B\\")
    send("\033[Bv")
    send("\033[A/")
    send("\033[A/")

    # draw a line below it (default to 1 moves)
    send("\033[B\033[B\033[B")
    send("\033[D\033[D\033[D\033[D\033[D+")
    send("\033[C-")
    send("\033[C+")

    # draw a line above it (default to 1 moves)
    send("\033[A\033[A\033[A\033[A\033[D+")
    send("\033[D\033[D\033[D-")
    send("\033[D\033[D\033[D+")

    # direct move, draw some text
    send("\033[1;12H testshouldn't see this")
    # direct move, clear to eol
    send("\033[1;17H\033[K")
    # direct move and overwrite
    send("\033[1;8Hmove")

    # some more moves (multiline/column), two spirals
    send("\033[2;30H/") # top left
    send("\033[2B\033[D>")
    send("\033[2B\033[D\\") # lower left
    send("\033[3C^")
    send("\033[3C/") # lower right
    send("\033[2A\033[D<")
    send("\033[2A\033[D\\") # top right
    send("\033[5Dv")
    # and a jump, then back, with 1's this time
    send("\033[4;34H*") # center
    send("\033[1A\033[1D|")
    send("\033[1A---")
    send("\033[1B|")
    send("\033[2B\033[1D|")
    send("\033[1B\033[4D---")
    send("\033[7D---")
    send("\033[1A\033[4D|")
    send("\033[2A\033[1D|")
    send("\033[1A---")
    # and the rest of the cross (with a 1C)
    send("\033[2B\033[3D---")
    send("\033[1C---")
    send("\033[1B\033[4D|")

    # move cursor to line 7, col 1, write a bunch of junk
    send("\033[7;1H")
    send("12345678901234567890\r\n")
    send("12345678901234567890\r\n")
    send("12345678901234567890\r\n")
    send("12345678901234567890\r\n")
    send("12345678901234567890\r\n")
    send("12345678901234567890\r\n")
    send("12345678901234567890\r\n")

    # move cursor to line 7, col 10, clear below
    send("\033[7;10H\033[J")

    # move cursor to line 7, col 1
    send("\033[7;1H")
    send("above should match below:\r\n")
    send("cursor move test\r\n")
    send("+ - +                        /---v---\\\r\n")
    send("\\   /                        |   |   |\r\n")
    send(" \\ /                         >---*---<\r\n")
    send("  v                          |   |   |\r\n")
    send("+ - +                        \\---^---/\r\n")

    # move cursor to line 1, col 30, write some inverted text
    send("\033[1;30H\033[7minver\033[0;7mted\033[0m not-inverted")
    # do it on line 8 too (without the clear all but inverted
    send("\033[8;30H\033[7minverted\033[0m not-inverted")

    # move back to end of output
    send("\033[14;1H")

async def insert_erase_test():
    # go home
    send("\033[H")
    # clear screen
    send("\033[J")

    # draw some lines
    send("inserterasetest\r\n")
    send("+v  +\r\n")
    send("  |||\r\n")
    send("   + ^ +\r\n")
    # do some inserts (1@)
    send("\033[1;7H\033[1@")
    send("\033[1;13H\033[1@")
    send("\033[2;2H\033[1@-\033[C\033[1@-")
    send("\033[B\033[2C\033[3D\033[1@x\033[C\033[1@x")
    # do some erases (1P)
    send("\033[2;5H\033[1P\033[1P")
    # and a couple erases (incl a longer one)
    send("\033[2B\033[4D\033[3P")
    send("\033[C-\033[C-")
    # then a couple multiple inserts
    send("\033[4D\033[2@")
    send("\033[2A\033[2D\033[2@")

    # print some text
    send("\033[3Bgood text\r\nbad text\r\nbext")
    # delete second line
    send("\033[A\033[M")
    # go to beginning of line, insert mode "more", then overwrite again
    send("\033[4D\033[4hmore \033[4lt")
    # insert line before first
    send("\033[A\033[1L\033[6Dfirst text")

    # move cursor to line 8, col 1
    send("\033[8;1H")
    send("above should match below:\r\n")
    send("insert erase test\r\n")
    send("  +-v-+\r\n")
    send("  |x|x|\r\n")
    send("  +-^-+\r\n")
    send("first text\r\n")
    send("good text\r\n")
    send("more text\r\n")

    # move back to end of output
    send("\033[15;1H")

async def terminfo_test():
    pass # todo

async def keypress():
    send("press a key...")
    await queue.get()

async def run():
    caps = await identify_test()
    await cursor_command_test()
    await keypress()
    await insert_erase_test()

    # wrap line for next
    send("\r\n")

def main():
    # we want raw input, but still to get break chars
    old_settings = termios.tcgetattr(sys.stdin)

    fd = sys.stdin.fileno()
    tty.setcbreak(fd)
    fl = fcntl.fcntl(fd, fcntl.F_GETFL)
    fcntl.fcntl(fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)

    curses.setupterm()

    try:
        # Register the file descriptor for read event
        loop = asyncio.get_event_loop()
        loop.add_reader(sys.stdin, reader)

        loop.run_until_complete(run())
        loop.close()
    finally:
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_settings)

if __name__ == '__main__':
    main()

"""
from tmux' ansicode.txt
Minimum requirements for VT100 emulation:


2) To enter data in VT100 mode, implement the 4 cursor keys and the 4 PF keys.
   It must be possible to enter ESC, TAB, BS, DEL, and LF from the keyboard.

  [A       Sent by the up-cursor key (alternately ESC O A)
  [B       Sent by the down-cursor key (alternately ESC O B)
  [C       Sent by the right-cursor key (alternately ESC O C)
  [D       Sent by the left-cursor key (alternately ESC O D)
  OP       PF1 key sends ESC O P
  OQ       PF2 key sends ESC O Q
  OR       PF3 key sends ESC O R
  OS       PF3 key sends ESC O S
  [c       Request for the terminal to identify itself
  [?1;0c   VT100 with memory for 24 by 80, inverse video character attribute
  [?1;2c   VT100 capable of 132 column mode, with bold+blink+underline+inverse

3) When doing full-screen editing on a VT100, implement directed erase, the
   numeric keypad in applications mode, and the limited scrolling region.
   The latter is needed to do insert/delete line functions without rewriting
   the screen.

  [0J     Erase from current position to bottom of screen inclusive
  [1J     Erase from top of screen to current position inclusive
  [2J     Erase entire screen (without moving the cursor)
  [0K     Erase from current position to end of line inclusive
  [1K     Erase from beginning of line to current position inclusive
  [2K     Erase entire line (without moving cursor)
  [12;24r   Set scrolling region to lines 12 thru 24.  If a linefeed or an
            INDex is received while on line 24, the former line 12 is deleted
            and rows 13-24 move up.  If a RI (reverse Index) is received while
            on line 12, a blank line is inserted there as rows 12-13 move down.
            All VT100 compatible terminals (except GIGI) have this feature.
  ESC =   Set numeric keypad to applications mode
  ESC >   Set numeric keypad to numbers mode
  OA      Up-cursor key    sends ESC O A after ESC = ESC [ ? 1 h
  OB      Down-cursor key  sends ESC O B    "      "         "
  OC      Right-cursor key sends ESC O B    "      "         "
  OB      Left-cursor key  sends ESC O B    "      "         "
  OM      ENTER key        sends ESC O M after ESC =
  Ol      COMMA on keypad  sends ESC O l    "      "   (that's lowercase L)
  Om      MINUS on keypad  sends ESC O m    "      "
  Op      ZERO on keypad   sends ESC O p    "      "
  Oq      ONE on keypad    sends ESC O q    "      "
  Or      TWO on keypad    sends ESC O r    "      "
  Os      THREE on keypad  sends ESC O s    "      "
  Ot      FOUR on keypad   sends ESC O t    "      "
  Ou      FIVE on keypad   sends ESC O u    "      "
  Ov      SIX on keypad    sends ESC O v    "      "
  Ow      SEVEN on keypad  sends ESC O w    "      "
  Ox      EIGHT on keypad  sends ESC O x    "      "
  Oy      NINE on keypad   sends ESC O y    "      "

4) If the hardware is capable of double width/double height:

  #3     Top half of a double-width double-height line
  #4     Bottom half of a double-width double-height line
  #5     Make line single-width (lines are set this way when cleared by ESC [ J)
  #6     Make line double-width normal height (40 or 66 characters)

      [0i    Print screen (all 24 lines) to the printer
      [4i    All received data goes to the printer (nothing to the screen)
      [5i    All received data goes to the screen (nothing to the printer)

"""

#echo 'Press any key to continue...'; read -k1 -s
