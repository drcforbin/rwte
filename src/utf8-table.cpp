#include <array>
#include <utility>

enum STATES
{
    STATE_GROUND,
    STATE_U3_B2_E0,
    STATE_U3_B2_ED,
    STATE_U4_B3_F0,
    STATE_U4_B3_F4,
    STATE_TAIL1,
    STATE_TAIL2,
    STATE_TAIL3
};

enum ACTIONS
{
    ACTION_NOOP,
    ACTION_EMIT,
    ACTION_SET_BYTE1_EMIT,
    ACTION_SET_BYTE2,
    ACTION_SET_BYTE3,
    ACTION_SET_BYTE2_TOP,
    ACTION_SET_BYTE3_TOP,
    ACTION_SET_BYTE4_TOP,
    ACTION_INVALID
};

static const unsigned char invalid_val = ACTION_INVALID << 4 | STATE_GROUND;

// returns an action/state pair if i is within the given range (inclusive),
// and returns the provided default if not.
constexpr unsigned char lut_entry(int low, int high,
        unsigned char action, unsigned char state, int i,
        unsigned char defval)
{
    return (low <= i && i <= high)? (action << 4 | state) : defval;
}

// returns an action/state pair if i is within the given range (inclusive),
// and returns the invalid_val if not.
constexpr unsigned char lut_entry(int low, int high,
        unsigned char action, unsigned char state, int i)
{
    return lut_entry(low, high, action, state, i, invalid_val);
}

// returns an action/state pair for i, when in the GROUND state
constexpr unsigned char GROUND_lut_entry(int i)
{
    return
        lut_entry(0x00, 0x7F, ACTION_EMIT,          STATE_GROUND, i,
        lut_entry(0xC2, 0xDF, ACTION_SET_BYTE2_TOP, STATE_TAIL1, i,
        lut_entry(0xE0, 0xE0, ACTION_NOOP,          STATE_U3_B2_E0, i,
        lut_entry(0xE1, 0xEC, ACTION_SET_BYTE3_TOP, STATE_TAIL2, i,
        lut_entry(0xED, 0xED, ACTION_SET_BYTE3_TOP, STATE_U3_B2_ED, i,
        lut_entry(0xEE, 0xEF, ACTION_SET_BYTE3_TOP, STATE_TAIL2, i,
        lut_entry(0xF0, 0xF0, ACTION_SET_BYTE4_TOP, STATE_U4_B3_F0, i,
        lut_entry(0xF1, 0xF3, ACTION_SET_BYTE4_TOP, STATE_TAIL3, i,
        lut_entry(0xF4, 0xF4, ACTION_SET_BYTE4_TOP, STATE_U4_B3_F4, i,
        invalid_val)))))))));
}

// returns an action/state pair for i, when in the U3_B2_E0 state
constexpr unsigned char U3_B2_E0_lut_entry(int i)
{
    return lut_entry(0xA0, 0xBF, ACTION_SET_BYTE2, STATE_TAIL1, i);
}

// returns an action/state pair for i, when in the U3_B2_ED state
constexpr unsigned char U3_B2_ED_lut_entry(int i)
{
    return lut_entry(0x80, 0x9F, ACTION_SET_BYTE2, STATE_TAIL1, i);
}

// returns an action/state pair for i, when in the U4_B3_F0 state
constexpr unsigned char U4_B3_F0_lut_entry(int i)
{
    return lut_entry(0x90, 0xBF, ACTION_SET_BYTE3, STATE_TAIL2, i);
}

// returns an action/state pair for i, when in the U4_B3_F4 state
constexpr unsigned char U4_B3_F4_lut_entry(int i)
{
    return lut_entry(0x80, 0x8F, ACTION_SET_BYTE3, STATE_TAIL2, i);
}

// returns an action/state pair for i, when in the TAIL1 state
constexpr unsigned char TAIL1_lut_entry(int i)
{
    return lut_entry(0x80, 0xBF, ACTION_SET_BYTE1_EMIT, STATE_GROUND, i);
}

// returns an action/state pair for i, when in the TAIL2 state
constexpr unsigned char TAIL2_lut_entry(int i)
{
    return lut_entry(0x80, 0xBF, ACTION_SET_BYTE2, STATE_TAIL1, i);
}

// returns an action/state pair for i, when in the TAIL3 state
constexpr unsigned char TAIL3_lut_entry(int i)
{
    return lut_entry(0x80, 0xBF, ACTION_SET_BYTE3, STATE_TAIL2, i);
}

// returns an array of bytes, where each element is the result
// of calling entryfn on the next element of the input array
template<int ...Is>
constexpr auto make_lut_array(unsigned char(*entryfn)(int),
        std::integer_sequence<int, Is...>)
{
    return std::array<unsigned char, sizeof...(Is)>{{
        entryfn(Is)...
    }};
}

// returns an array of 256 bytes, where each element is the result
// of calling entryfn on the byte's index in the array
constexpr auto make_lut_array(unsigned char(*entryfn)(int))
{
    return make_lut_array(entryfn,
        std::make_integer_sequence<int, 256>());
}

extern const std::array<unsigned char, 256> UTF8_TRANSITIONS[8] = {
    make_lut_array(GROUND_lut_entry),
    make_lut_array(U3_B2_E0_lut_entry),
    make_lut_array(U3_B2_ED_lut_entry),
    make_lut_array(U4_B3_F0_lut_entry),
    make_lut_array(U4_B3_F4_lut_entry),
    make_lut_array(TAIL1_lut_entry),
    make_lut_array(TAIL2_lut_entry),
    make_lut_array(TAIL3_lut_entry)
};
