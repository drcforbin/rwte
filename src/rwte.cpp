#include "lua/config.h"
#include "lua/state.h"
#include "rw/logging.h"
#include "rwte/reactorctrl.h"
#include "rwte/rwte.h"
#include "rwte/term.h"
#include "rwte/tty.h"
#include "rwte/window.h"

#include <memory>
#include <sys/wait.h>

// todo: mark [[noreturn]] funcs
// todo: std::starts_with/ends_with
// todo: std::span
// todo: std::bit_cast
// todo: std::shift_left/shift_right
// todo: more std::string_view
// todo: memset/memcpy/memmove
// todo: std::exit and EXIT_x
// todo: nest impl classes
// todo: use override keyword
// todo: look for initializer in if or switch
// todo: bench codecvt vs utf8

// globals
Options options;
std::unique_ptr<Rwte> rwte;
lua_State* g_L = nullptr;

#define LOGGER() (rw::logging::get("rwte"))

// default values to use if we don't have
// a default value in config
constexpr float DEFAULT_BLINK_RATE = 0.6;

Rwte::Rwte(std::shared_ptr<event::Bus> bus, reactor::ReactorCtrl* ctrl) :
    m_bus(std::move(bus)),
    m_ctrl(ctrl),
    m_refreshReg(m_bus->reg<event::Refresh, Rwte, &Rwte::onrefresh>(this)),
    m_lua(std::make_shared<lua::State>())
{
    m_lua->openlibs();
}

Rwte::~Rwte()
{
    m_bus->unreg<event::Refresh>(m_refreshReg);
}

void Rwte::refresh()
{
    if (options.throttledraw) {
        m_ctrl->queue_refresh(1.0 / 60.0);
    } else {
        // for wayland, we let the window throttle
        if (auto window = m_window.lock())
            window->draw();
    }
}

void Rwte::start_blink()
{
    float rate = lua::config::get_float(
            "blink_rate", DEFAULT_BLINK_RATE);

    m_ctrl->start_blink(rate);

    // todo: necessary?
    // reset the timer if it's already active
    // (so we don't blink until idle)
    // m_ctrl->stop_blink();
    // m_ctrl->start_blink();
}

void Rwte::stop_blink()
{
    m_ctrl->stop_blink();
}

void Rwte::child_ended()
{
    // as long as something's exited, reap it
    for (;;) {
        int status = 0;
        if (waitpid(WAIT_ANY, &status, WNOHANG | WUNTRACED | WCONTINUED) == -1) {
            // stop unless we were interrupted
            if (errno != EINTR) {
                break;
            } else {
                if (WIFEXITED(status) && WEXITSTATUS(status))
                    LOGGER()->warn("child exited with status {}", WEXITSTATUS(status));
                else if (WIFSIGNALED(status))
                    LOGGER()->info("child terminated to to signal {}", WTERMSIG(status));
            }
        }
    }
}

void Rwte::flushcb()
{
    if (auto window = m_window.lock())
        window->draw();
}

void Rwte::blinkcb()
{
    if (auto term = m_term.lock())
        term->blink();
}

void Rwte::onrefresh(const event::Refresh& evt)
{
    refresh();
}
