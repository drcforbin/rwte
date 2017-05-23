#ifndef SIGWATCHER_H
#define SIGWATCHER_H

#include <ev++.h>

// listens to SIGTERM/QUIT/INT and tries to exit cleanly, by stopping the main loop
class SigWatcher
{
    public:
        SigWatcher();

        // handle signals, exiting main_loop so we can clean up
        void sigcb(ev::sig &w, int);

    private:
        ev::sig m_sig_term;
        ev::sig m_sig_int;
        ev::sig m_sig_hup;
};

#endif // SIGWATCHER_H
