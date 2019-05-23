#!/usr/bin/python

import collections
import enum

@enum.unique
class State(enum.IntEnum):
    GROUND = 0
    U3_B2_E0 = 1
    U3_B2_ED = 2
    U4_B3_F0 = 3
    U4_B3_F4 = 4
    TAIL1 = 5
    TAIL2 = 6
    TAIL3 = 7

@enum.unique
class Action(enum.IntEnum):
    NOOP = 0
    EMIT = 1
    SET_BYTE1_EMIT = 2
    SET_BYTE2 = 3
    SET_BYTE3 = 4
    SET_BYTE2_TOP = 5
    SET_BYTE3_TOP = 6
    SET_BYTE4_TOP = 7
    INVALID = 8

ByteRange = collections.namedtuple("ByteRange", ["low", "high"])
Entry = collections.namedtuple("Entry", ["key", "action", "state"])

table = {
    State.GROUND: [
        Entry(ByteRange(0x00, 0x7F), Action.EMIT,          State.GROUND),
        Entry(ByteRange(0xC2, 0xDF), Action.SET_BYTE2_TOP, State.TAIL1),
        Entry(ByteRange(0xE0, 0xE0), Action.NOOP,          State.U3_B2_E0),
        Entry(ByteRange(0xE1, 0xEC), Action.SET_BYTE3_TOP, State.TAIL2),
        Entry(ByteRange(0xED, 0xED), Action.SET_BYTE3_TOP, State.U3_B2_ED),
        Entry(ByteRange(0xEE, 0xEF), Action.SET_BYTE3_TOP, State.TAIL2),
        Entry(ByteRange(0xF0, 0xF0), Action.SET_BYTE4_TOP, State.U4_B3_F0),
        Entry(ByteRange(0xF1, 0xF3), Action.SET_BYTE4_TOP, State.TAIL3),
        Entry(ByteRange(0xF4, 0xF4), Action.SET_BYTE4_TOP, State.U4_B3_F4)
    ],
    State.U3_B2_E0: [
        Entry(ByteRange(0xA0, 0xBF), Action.SET_BYTE2, State.TAIL1)
    ],
    State.U3_B2_ED: [
        Entry(ByteRange(0x80, 0x9F), Action.SET_BYTE2, State.TAIL1)
    ],
    State.U4_B3_F0: [
        Entry(ByteRange(0x90, 0xBF), Action.SET_BYTE3, State.TAIL2)
    ],
    State.U4_B3_F4: [
        Entry(ByteRange(0x80, 0x8F), Action.SET_BYTE3, State.TAIL2)
    ],
    State.TAIL1: [
        Entry(ByteRange(0x80, 0xBF), Action.SET_BYTE1_EMIT, State.GROUND)
    ],
    State.TAIL2: [
        Entry(ByteRange(0x80, 0xBF), Action.SET_BYTE2, State.TAIL1)
    ],
    State.TAIL3: [
        Entry(ByteRange(0x80, 0xBF), Action.SET_BYTE3, State.TAIL2)
    ]
}

invalid_val = Action.INVALID.value << 4 | State.GROUND.value

with open("utf8-table.cpp", "w") as f:
    f.write("extern const unsigned char UTF8_TRANSITIONS[8][256] = {\n")
    for state in State:
        state_entries = table[state]

        arr = [invalid_val for i in range(256)]

        for entry in state_entries:
            for b in range(entry.key.low, entry.key.high + 1):
                arr[b] = entry.action.value << 4 | entry.state.value

        f.write("    {\n")
        for row in range(16):
            start = row * 16
            end = start + 16
            line = ", ".join("0x{:02X}".format(i) for i in arr[start:end])
            f.write("        {},\n".format(line))
        f.write("    },\n")
    f.write("};\n")
