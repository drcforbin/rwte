-- set the magic bit for a color value
local function truecolor(color)
    return bit32.bor(color, 0x1000000)
end

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
        colors[k] = truecolor(colors[k])
    end

    return colors
end

config = {
    -- default title
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

    -- pango font desc
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

    -- default size
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
    cursor_thickness = 2

}

-- set up some logging defaults
logging.get("term"):level(logging.debug)
logging.get("tty"):level(logging.debug)

logger = logging.get("config")
--logger:info("config complete")

term.mouse_press(function(col, row, button, mod)
    --local shift, ctrl, alt, logo
    --if mod.shift then shift = "true" else shift = "false" end
    --if mod.ctrl then ctrl = "true" else ctrl = "false" end
    --if mod.alt then alt = "true" else alt = "false" end
    --if mod.logo then logo = "true" else logo = "false" end
    --logger:info(string.format("mouse_press: %d,%d - %d - shift %s, ctrl %s, alt %s, logo %s",
    --    col, row, button, shift, ctrl, alt, logo))

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

local anymod_keycmds = {
    [term.keys.Home] = "\027[1~",
    [term.keys.Insert] = "\027[2~",
    [term.keys.Delete] = "\027[3~",
    [term.keys.End] = "\027[4~",
    [term.keys.Page_Up] = "\027[5~",
    [term.keys.Page_Down] = "\027[6~",
    [term.keys.F1] = "\027OP",
    [term.keys.F2] = "\027OQ",
    [term.keys.F3] = "\027OR",
    [term.keys.F4] = "\027OS",
    [term.keys.F5] = "\027[15~",
    [term.keys.F6] = "\027[17~",
    [term.keys.F7] = "\027[18~",
    [term.keys.F8] = "\027[19~",
    [term.keys.F9] = "\027[20~",
    [term.keys.F10] = "\027[21~",
    [term.keys.F11] = "\027[23~",
    [term.keys.F12] = "\027[24~",
    [term.keys.ISO_Left_Tab] = "\027[Z"
}

local appkeypad_numlock_keycmds = {
    [term.keys.KP_Multiply] = "\027Oj",
    [term.keys.KP_Add] = "\027Ok",
    [term.keys.KP_Subtract] = "\027Om",
    [term.keys.KP_Decimal] = "\027On",
    [term.keys.KP_Divide] = "\027Oo",
    [term.keys.KP_0] = "\027Op",
    [term.keys.KP_1] = "\027Oq",
    [term.keys.KP_2] = "\027Or",
    [term.keys.KP_3] = "\027Os",
    [term.keys.KP_4] = "\027Ot",
    [term.keys.KP_5] = "\027Ou",
    [term.keys.KP_6] = "\027Ov",
    [term.keys.KP_7] = "\027Ow",
    [term.keys.KP_8] = "\027Ox",
    [term.keys.KP_9] = "\027Oy"
}

term.key_press(function(sym, mod)
    -- check for special commands
    if mod.shift and mod.ctrl then
        if sym == term.keys.C then
            term.clipcopy()
            return true
        elseif sym == term.keys.V then
            term.clippaste()
            return true
        elseif sym == term.keys.Y then
            term.selpaste()
            return true
        end
    end

    local appkeypad = term.mode(term.modes.MODE_APPKEYPAD)
    local appcursor = term.mode(term.modes.MODE_APPCURSOR)
    local crlf = term.mode(term.modes.MODE_CRLF)

    local cmd = nil

    -- a few regular keys
    if sym == term.keys.BackSpace then
        if mod.alt then
            cmd = "\027\127"
        else
            cmd = "\127"
        end
    elseif sym == term.keys.Return then
        if not crlf then
            if not alt then
                cmd = "\r"
            else
                cmd = "\027\r"
            end
        else
            if not alt then
                cmd = "\r\n"
            else
                cmd = "\027\r\n"
            end
        end
    end

    -- check anymod keys
    if not cmd then
        cmd = anymod_keycmds[sym]
    end

    -- keypad keys
    if not cmd then
        if sym == term.keys.KP_Enter then
            if appkeypad then
                if mod.numlock then
                    cmd = "\027OM"
                end
            else
                if crlf then
                    cmd = "\r\n"
                else
                    cmd = "\r"
                end
            end
        elseif sym == term.keys.KP_Home then
            if mod.shift then
                if appcursor then
                    cmd = "\027[1;2H"
                else
                    cmd = "\027[2J"
                end
            else
                if appcursor then
                    cmd = "\027[1~"
                else
                    cmd = "\027[H"
                end
            end
        elseif sym == term.keys.KP_Up then
            if appkeypad then
                cmd = "\027Ox"
            elseif appcursor then
                cmd = "\027OA"
            else
                cmd = "\027[A"
            end
        elseif sym == term.keys.KP_Down then
            if appkeypad then
                cmd = "\027Or"
            elseif appcursor then
                cmd = "\027OB"
            else
                cmd = "\027[B"
            end
        elseif sym == term.keys.KP_Left then
            if appkeypad then
                cmd = "\027Ot"
            elseif appcursor then
                cmd = "\027OD"
            else
                cmd = "\027[D"
            end
        elseif sym == term.keys.KP_Right then
            if appkeypad then
                cmd = "\027Ov"
            elseif appcursor then
                cmd = "\027OC"
            else
                cmd = "\027[C"
            end
        elseif sym == term.keys.KP_Prior then
            if mod.shift then
                cmd = "\027[5;2~"
            else
                cmd = "\027[5~"
            end
        elseif sym == term.keys.KP_Begin then
            cmd = "\027[E"
        elseif sym == term.keys.KP_End then
            if mod.ctrl then
                if appkeypad then
                    cmd = "\027[1;5F"
                else
                    cmd = "\027[J"
                end
            elseif mod.shift then
                if appkeypad then
                    cmd = "\027[1;2F"
                else
                    cmd = "\027[K"
                end
            else
                cmd = "\027[4~"
            end
        elseif sym == term.keys.KP_Next then
            if mod.shift then
                cmd = "\027[6;2~"
            else
                cmd = "\027[6~"
            end
        elseif sym == term.keys.KP_Insert then
            if mod.shift then
                if appkeypad then
                    cmd = "\027[2;2~"
                else
                    cmd = "\027[4l"
                end
            elseif mod.ctrl then
                if appkeypad then
                    cmd = "\027[2;5~"
                else
                    cmd = "\027[L"
                end
            else
                if appkeypad then
                    cmd = "\027[2~"
                else
                    cmd = "\027[4h"
                end
            end
        elseif sym == term.keys.KP_Delete then
            if mode.ctrl then
                if appkeypad then
                    cmd = "\027[3;5~"
                else
                    cmd = "\027[M"
                end
            elseif mod.shift then
                if appkeypad then
                    cmd = "\027[3;2~"
                else
                    cmd = "\027[2K"
                end
            else
                if appkeypad then
                    cmd = "\027[3~"
                else
                    cmd = "\027[P"
                end
            end
        end

        -- keypad numbers
        if not cmd and ( appkeypad and mod.numlock ) then
            cmd = appkeypad_numlock_keycmds[sym]
        end
    end

    if cmd then
        term.send(cmd)
        return true
    end

    return false
end)
