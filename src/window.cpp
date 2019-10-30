#include "lua/window.h"
#include "rwte/logging.h"
#include "rwte/rwte.h"
#include "rwte/term.h"
#include "rwte/tty.h"
#include "rwte/utf8.h"
#include "rwte/window-internal.h"
#include "rwte/window.h"

#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>

#define LOGGER() (logging::get("window"))

WindowError::WindowError(const std::string& arg) :
    std::runtime_error(arg)
{}

WindowError::WindowError(const char* arg) :
    std::runtime_error(arg)
{}

WindowError::~WindowError()
{}

void process_key(uint32_t key, term::Term* trm, Tty* tty, xkb_state* state,
        xkb_compose_state* compose_state, const term::keymod_state& keymod)
{
    auto& mode = trm->mode();
    if (mode[term::MODE_KBDLOCK]) {
        LOGGER()->info("key press while locked {}", key);
        return;
    }

    xkb_keysym_t ksym = xkb_state_key_get_one_sym(state, key);

    // The buffer will be null-terminated, so n >= 2 for 1 actual character.
    std::array<char, 128> buffer{};

    std::size_t len = 0;
    bool composed = false;
    if (compose_state &&
            xkb_compose_state_feed(compose_state, ksym) == XKB_COMPOSE_FEED_ACCEPTED) {
        switch (xkb_compose_state_get_status(compose_state)) {
            case XKB_COMPOSE_NOTHING:
                break;
            case XKB_COMPOSE_COMPOSING:
                return;
            case XKB_COMPOSE_COMPOSED:
                len = xkb_compose_state_get_utf8(compose_state,
                        buffer.data(), buffer.size() - 1);
                ksym = xkb_compose_state_get_one_sym(compose_state);
                composed = true;
                break;
            case XKB_COMPOSE_CANCELLED:
                xkb_compose_state_reset(compose_state);
                return;
        }
    }

    // todo: move arrow keys
    switch (ksym) {
        case XKB_KEY_Left:
        case XKB_KEY_Up:
        case XKB_KEY_Right:
        case XKB_KEY_Down:
            buffer[0] = '\033';

            if (keymod[term::MOD_SHIFT] || keymod[term::MOD_CTRL]) {
                if (!keymod[term::MOD_CTRL])
                    buffer[1] = '[';
                else
                    buffer[1] = 'O';

                // todo: safetyfy...tightly coupled to val of XKB_KEY_Left
                buffer[2] = "dacb"[ksym - XKB_KEY_Left];
            } else {
                if (!mode[term::MODE_APPCURSOR])
                    buffer[1] = '[';
                else
                    buffer[1] = 'O';

                // todo: safetyfy...tightly coupled to val of XKB_KEY_Left
                buffer[2] = "DACB"[ksym - XKB_KEY_Left];
            }

            trm->send({buffer.data(), 3});
            return;
    }

    auto L = rwte->lua();
    if (lua::window::call_key_press(L.get(), ksym, keymod))
        return;

    if (!composed)
        len = xkb_state_key_get_utf8(state, key, buffer.data(), buffer.size() - 1);

    if (len == 1 && keymod[term::MOD_ALT]) {
        if (mode[term::MODE_8BIT]) {
            if (buffer[0] < 0177) {
                char32_t c = buffer[0] | 0x80;
                len = utf8encode(c, buffer.begin()) - buffer.begin();
            }
        } else {
            buffer[1] = buffer[0];
            buffer[0] = '\033';
            len = 2;
        }
    }

    tty->write({buffer.data(), len});
}
