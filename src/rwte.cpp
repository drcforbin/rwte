#include "lua/config.h"
#include "lua/state.h"
#include "rwte/logging.h"
#include "rwte/rwte.h"
#include "rwte/term.h"
#include "rwte/tty.h"
#include "rwte/window.h"

#include <memory>

// globals
Window window;
Options options;
Rwte rwte;
std::unique_ptr<Term> g_term;
std::unique_ptr<Tty> g_tty;
lua_State * g_L = nullptr;

#define LOGGER() (logging::get("rwte"))

// default values to use if we don't have
// a default value in config
static const float DEFAULT_BLINK_RATE = 0.6;

Options::Options() :
    cmd(nullptr),
    title("rwte"),
    noalt(false)
{ }

Rwte::Rwte() :
    m_lua(std::make_shared<lua::State>())
{
    m_lua->openlibs();

    m_child.set<Rwte,&Rwte::childcb>(this);
    m_flush.set<Rwte,&Rwte::flushcb>(this);
    m_blink.set<Rwte,&Rwte::blinkcb>(this);
}

void Rwte::resize(uint16_t width, uint16_t height)
{
    if (width == 0)
        width = window.width();
    if (height == 0)
        height = window.height();

    if (window.width() != width || window.height() != height)
    {
        window.resize(width, height);
        g_term->resize(window.cols(), window.rows());

        if (g_tty)
            g_tty->resize();
    }
}

void Rwte::watch_child(pid_t pid)
{
    LOGGER()->debug("watching child {}", pid);
    m_child.start(pid);
}

void Rwte::refresh()
{
    if (!m_flush.is_active())
        m_flush.start(1.0/60.0);
}

void Rwte::start_blink()
{
    if (!m_blink.is_active())
    {
        float rate = lua::config::get_float(
                "blink_rate", DEFAULT_BLINK_RATE);

        m_blink.start(rate, rate);
    }
    else
    {
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

void Rwte::childcb(ev::child &w, int)
{
    if (!WIFEXITED(w.rstatus) || WEXITSTATUS(w.rstatus))
        LOGGER()->warn("child finished with error {}", w.rstatus);
    else
        LOGGER()->info("child exited");
    w.loop.break_loop(ev::ALL);
}

void Rwte::flushcb(ev::timer &, int)
{
    window.draw();
}

void Rwte::blinkcb(ev::timer &, int)
{
    g_term->blink();
}

