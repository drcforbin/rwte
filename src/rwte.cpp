#include "lua/config.h"
#include "lua/state.h"
#include "rwte/logging.h"
#include "rwte/rwte.h"
#include "rwte/term.h"
#include "rwte/tty.h"
#include "rwte/window.h"

#include <memory>

// todo: mark [[noreturn]] funcs
// todo: std::starts_with/ends_with
// todo: std::span
// todo: std::bit_cast
// todo: std::shift_left/shift_right

// globals
Options options;
std::unique_ptr<Rwte> rwte;
lua_State* g_L = nullptr;

#define LOGGER() (logging::get("rwte"))

// default values to use if we don't have
// a default value in config
static const float DEFAULT_BLINK_RATE = 0.6;

Rwte::Rwte(std::shared_ptr<event::Bus> bus) :
    m_bus(std::move(bus)),
    m_refreshReg(m_bus->reg<event::Refresh, Rwte, &Rwte::onrefresh>(this)),
    m_lua(std::make_shared<lua::State>())
{
    m_lua->openlibs();

    m_child.set<Rwte, &Rwte::childcb>(this);
    m_flush.set<Rwte, &Rwte::flushcb>(this);
    m_blink.set<Rwte, &Rwte::blinkcb>(this);
}

Rwte::~Rwte()
{
    m_bus->unreg<event::Refresh>(m_refreshReg);
}

void Rwte::watch_child(pid_t pid)
{
    LOGGER()->debug("watching child {}", pid);
    m_child.start(pid);
}

void Rwte::refresh()
{
    // with xcb, we throttle drawing here
    if (options.throttledraw) {
        if (!m_flush.is_active())
            m_flush.start(1.0 / 60.0);
    } else {
        // for wayland, we let the window throttle
        if (auto window = m_window.lock())
            window->draw();
    }
}

void Rwte::start_blink()
{
    if (!m_blink.is_active()) {
        float rate = lua::config::get_float(
                "blink_rate", DEFAULT_BLINK_RATE);

        m_blink.start(rate, rate);
    } else {
        // reset the timer if it's already active
        // (so we don't blink until idle)
        m_blink.stop();
        m_blink.start();
    }
}

void Rwte::stop_blink()
{
    if (m_blink.is_active())
        m_blink.stop();
}

void Rwte::onrefresh(const event::Refresh& evt)
{
    refresh();
}

void Rwte::childcb(ev::child& w, int)
{
    if (WIFEXITED(w.rstatus) && WEXITSTATUS(w.rstatus))
        LOGGER()->warn("child exited with status {}", WEXITSTATUS(w.rstatus));
    else if (WIFSIGNALED(w.rstatus))
        LOGGER()->info("child terminated to to signal {}", WTERMSIG(w.rstatus));
    w.loop.break_loop(ev::ALL);
}

void Rwte::flushcb(ev::timer&, int)
{
    if (auto window = m_window.lock())
        window->draw();
}

void Rwte::blinkcb(ev::timer&, int)
{
    if (auto term = m_term.lock())
        term->blink();
}
