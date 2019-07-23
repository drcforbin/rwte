#include "rwte/logging.h"
#include "rwte/sigwatcher.h"

#include <ev++.h>
#include <sys/wait.h>

#define LOGGER() (logging::get("sigwatcher"))

// listen to SIGTERM/QUIT/INT and try to exit cleanly, by stopping the main loop
SigWatcher::SigWatcher()
{
    m_sig_term.set<SigWatcher, &SigWatcher::sigcb>(this);
    m_sig_term.start(SIGTERM);

    m_sig_int.set<SigWatcher, &SigWatcher::sigcb>(this);
    m_sig_int.start(SIGINT);

    m_sig_hup.set<SigWatcher, &SigWatcher::sigcb>(this);
    m_sig_hup.start(SIGHUP);
}

void SigWatcher::sigcb(ev::sig& w, int)
{
    switch (w.signum) {
        case SIGTERM:
            LOGGER()->info("got a SIGTERM, stopping");
            w.loop.break_loop(ev::ALL);
            break;
        case SIGINT:
            LOGGER()->info("got a SIGINT, stopping");
            w.loop.break_loop(ev::ALL);
            break;
        case SIGHUP:
            LOGGER()->info("got a SIGHUP, stopping");
            w.loop.break_loop(ev::ALL);
            break;
    }
}
