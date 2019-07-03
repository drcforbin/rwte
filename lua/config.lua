local util = require("util")

-- set up some logging defaults
logging.get("term").level = logging.debug
logging.get("tty").level = logging.debug
logging.get("xcbwindow").level = logging.debug
logging.get("wlwindow").level = logging.debug

-- solarized
local function color_table()
    local base03 = 0x002b36
    local base02 = 0x073642
    local base01 = 0x586e75
    local base00 = 0x657b83
    local base0 = 0x839496
    local base1 = 0x93a1a1
    local base2 = 0xeee8d5
    local base3 = 0xfdf6e3
    local yellow = 0xb58900
    local orange = 0xcb4b16
    local red = 0xdc322f
    local magenta = 0xd33682
    local violet = 0x6c71c4
    local blue = 0x268bd2
    local cyan = 0x2aa198
    local green = 0x859900

    local colors = {
        -- 8 normal colors
        [0] = base02,
        [1] = red,
        [2] = green,
        [3] = yellow,
        [4] = blue,
        [5] = magenta,
        [6] = cyan,
        [7] = base2,
        -- 8 bright colors
        [8] = base03,
        [9] = orange,
        [10] = base01,
        [11] = base00,
        [12] = base0,
        [13] = violet,
        [14] = base1,
        [15] = base3,
        -- black
        [255] = 0,
        -- a couple others
        [256] = 0xcccccc,
        [257] = 0x555555
    }

    -- distinguishing indexes into the color table
    -- from color values is done by setting a magic bit
    for k,v in pairs(colors) do
        colors[k] = util.truecolor(colors[k])
    end

    return colors
end

-- global config table
config = {
    -- default title; may be overridden by command line arg
    title = "rwte",

    -- colors!
    colors = color_table(),

    -- index in table for real black
    black_idx = 255,

    -- default colors (can be color table index or actual
    -- values, but they need the magic bit)
    -- foreground, background, cursor, reverse cursor
    default_fg = 11,
    default_bg = 15,
    default_cs = 1,
    default_rcs = 257,

    -- shell precedence:
    -- 1: program passed with -e
    -- 2: SHELL environment variable
    -- 3: value of shell in /etc/passwd
    -- 4: value of config.default_shell
    default_shell = "/usr/bin/sh",

    -- if using a terminal, stty args
    stty_args = "stty raw pass8 nl -echo -iexten -cstopb 38400",

    -- default TERM value
    term_name = "st-256color",

    -- pango font desc; may be overridden by command line arg
    font = "Source Code Pro 10",

    -- kerning / character bounding-box multipliers
    cw_scale = 1.0,
    ch_scale = 1.0,

    -- border width, in pixels
    border_px = 2,

    -- spaces per tab. When changing this value, don't forget to change the
    -- »it« value in rwte.info and reinstall (e.g., it#8 or it#4). Also make
    -- the kernel is not expanding tabs. When running `stty -- -a` »tab0« should
    -- appear. You can tell the terminal to not expand tabs by running 'stty tabs'
    tab_spaces = 8,

    -- word delimiters for selection
    word_delimiters = " '`\"()[]{}<>|",

    -- selection timeouts (in milliseconds)
    -- double click for word select
    dclick_timeout = 300,
    -- triple click for line select
    tclick_timeout = 600,

    -- bell volume. must be between -100 and 100,
    -- can be 0 or missing to disable
    bell_volume = 50,

    -- default size; may be overridden by command line arg
    default_cols = 80,
    default_rows = 24,

    -- identification sequence returned in DA and DECID
    -- (6c means we're a VT102)
    term_id = "\027[?6c",

    -- whether alt screens are used
    allow_alt_screen = true,

    -- cursor. choices for type are "blink block", "steady block",
    -- "blink under", "steady under", "blink bar", and "steady bar",
    -- defaulting to "steady block" if unspecified
    cursor_type = "steady block",
    cursor_thickness = 2,

    -- rate at which text / the cursor blinks, in seconds
    blink_rate = 0.6
}

window.mouse_press(function(col, row, button, mod)
    -- handle wheel, ignoring modifiers
    if button == 4 then -- wheel up
        term.send("\025")
        return true
    elseif button == 5 then -- wheel down
        term.send("\005")
        return true
    end

    return false
end)

-- key tables

local appkeypad_numlock_keycmds = {
    [window.keys.KP_Multiply] = "\027Oj",
    [window.keys.KP_Add] = "\027Ok",
    [window.keys.KP_Subtract] = "\027Om",
    [window.keys.KP_Decimal] = "\027On",
    [window.keys.KP_Divide] = "\027Oo",
    [window.keys.KP_0] = "\027Op",
    [window.keys.KP_1] = "\027Oq",
    [window.keys.KP_2] = "\027Or",
    [window.keys.KP_3] = "\027Os",
    [window.keys.KP_4] = "\027Ot",
    [window.keys.KP_5] = "\027Ou",
    [window.keys.KP_6] = "\027Ov",
    [window.keys.KP_7] = "\027Ow",
    [window.keys.KP_8] = "\027Ox",
    [window.keys.KP_9] = "\027Oy",
    [window.keys.KP_Enter] = "\027OM"
}

local appkeypad_ctrl_keycmds = {
    [window.keys.KP_End] = "\027[1;5F",
    [window.keys.KP_Insert] = "\027[2;5~",
    [window.keys.KP_Delete] = "\027[3;5~"
}

local appkeypad_shift_keycmds = {
    [window.keys.KP_End] = "\027[1;2F",
    [window.keys.KP_Insert] = "\027[2;2~",
    [window.keys.KP_Delete] = "\027[3;2~"
}

local appcursor_shift_keycmds = {
    [window.keys.KP_Home] = "\027[1;2H"
}

local alt_keycmds = {
    [window.keys.BackSpace] = "\027\127",
    [window.keys.Return] = "\027\r"
}

local shift_keycmds = {
    [window.keys.KP_Page_Up] = "\027[5;2~",
    [window.keys.KP_Page_Down] = "\027[6;2~",
    [window.keys.KP_Home] = "\027[2J",
    [window.keys.KP_End] = "\027[K",
    [window.keys.KP_Insert] = "\027[4l",
    [window.keys.KP_Delete] = "\027[2K"
}

local ctrl_keycmds = {
    [window.keys.KP_Insert] = "\027[L",
    [window.keys.KP_Delete] = "\027[M",
    [window.keys.KP_End] = "\027[J"
}

local appkeypad_keycmds = {
    [window.keys.KP_Up] = "\027Ox",
    [window.keys.KP_Down] = "\027Or",
    [window.keys.KP_Left] = "\027Ot",
    [window.keys.KP_Right] = "\027Ov",
    [window.keys.KP_Insert] = "\027[2~",
    [window.keys.KP_Delete] = "\027[3~"
}

local appcursor_keycmds = {
    [window.keys.KP_Home] = "\027[1~",
    [window.keys.KP_Up] = "\027OA",
    [window.keys.KP_Down] = "\027OB",
    [window.keys.KP_Left] = "\027OD",
    [window.keys.KP_Right] = "\027OC"
}

local anymod_keycmds = {
    [window.keys.Home] = "\027[1~",
    [window.keys.Insert] = "\027[2~",
    [window.keys.Delete] = "\027[3~",
    [window.keys.End] = "\027[4~",
    [window.keys.KP_End] = "\027[4~",
    [window.keys.Page_Up] = "\027[5~",
    [window.keys.KP_Page_Up] = "\027[5~",
    [window.keys.Page_Down] = "\027[6~",
    [window.keys.KP_Page_Down] = "\027[6~",
    [window.keys.F1] = "\027OP",
    [window.keys.F2] = "\027OQ",
    [window.keys.F3] = "\027OR",
    [window.keys.F4] = "\027OS",
    [window.keys.F5] = "\027[15~",
    [window.keys.F6] = "\027[17~",
    [window.keys.F7] = "\027[18~",
    [window.keys.F8] = "\027[19~",
    [window.keys.F9] = "\027[20~",
    [window.keys.F10] = "\027[21~",
    [window.keys.F11] = "\027[23~",
    [window.keys.F12] = "\027[24~",
    [window.keys.ISO_Left_Tab] = "\027[Z",
    [window.keys.BackSpace] = "\127",
    [window.keys.KP_Begin] = "\027[E",
    [window.keys.Return] = "\r",
    [window.keys.KP_Enter] = "\r",
    [window.keys.KP_Home] = "\027[H",
    [window.keys.KP_Up] = "\027[A",
    [window.keys.KP_Down] = "\027[B",
    [window.keys.KP_Left] = "\027[D",
    [window.keys.KP_Right] = "\027[C",
    [window.keys.KP_Insert] = "\027[4h",
    [window.keys.KP_Delete] = "\027[P"
}

-- uses the key mod and term.mode to search through
-- the key tables for a mapping for a key sym; returns
-- the first one found
local function lookup_key(sym, mod)
    local cmd = nil

    -- just do crlf here, rather than adding more tables
    if term.mode(term.modes.MODE_CRLF) then
        if sym == window.keys.Return then
            if not mod.alt then
                cmd = "\r\n"
            else
                cmd = "\027\r\n"
            end
        elseif sym == window.keys.KP_Enter then
            cmd = "\r\n"
        end
    end

    if not cmd then
        local appkeypad = term.mode(term.modes.MODE_APPKEYPAD)
        if appkeypad then
            if mod.numlock then
                cmd = appkeypad_numlock_keycmds[sym]
            end
            if not cmd and mod.ctrl then
                cmd = appkeypad_ctrl_keycmds[sym]
            end
            if not cmd and mod.shift then
                cmd = appkeypad_shift_keycmds[sym]
            end
        end
        if not cmd then
            local appcursor = term.mode(term.modes.MODE_APPCURSOR)
            if appcursor and mod.shift then
                cmd = appkeypad_shift_keycmds[sym]
            end
            if not cmd and mod.alt then
                cmd = alt_keycmds[sym]
            end
            if not cmd and mod.shift then
                cmd = shift_keycmds[sym]
            end
            if not cmd and mod.ctrl then
                cmd = ctrl_keycmds[sym]
            end
            if not cmd and appkeypad then
                cmd = appkeypad_keycmds[sym]
            end
            if not cmd and appcursor then
                cmd = appcursor_keycmds[sym]
            end
            if not cmd then
                cmd = anymod_keycmds[sym]
            end
        end
    end

    return cmd
end

window.key_press(function(sym, mod)
    -- check for special commands
    if mod.shift and mod.ctrl then
        if sym == window.keys.C then
            term.clipcopy()
            return true
        elseif sym == window.keys.V then
            window.clippaste()
            return true
        elseif sym == window.keys.Y then
            window.selpaste()
            return true
        end
    end

    local cmd = lookup_key(sym, mod)

    if cmd then
        term.send(cmd)
        return true
    end

    return false
end)

