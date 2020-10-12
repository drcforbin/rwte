#include "rwte/sigevent.h"

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <errno.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <unistd.h>

// todo: clean up all these exposed errors

static std::atomic_uint64_t SIGS_PENDING = 0;
static std::atomic_int SIGNAL_FD = -1;

extern "C" void sig_handler(int sig)
{
    SIGS_PENDING.fetch_or(1 << uint64_t(sig), std::memory_order_relaxed);

    int fd = SIGNAL_FD.load(std::memory_order_relaxed);
    std::array<unsigned char, 8> buf = {1, 0, 0, 0, 0, 0, 0, 0};

    // ignoring write result. there's nothing we
    // can do here but panic anyway
    write(fd, &buf, buf.size());
}

void connect_handler(int sig)
{
    // we only have space for 64 signals...
    // todo: throw if sig > 64

    sigset_t mask;
    sigfillset(&mask);

    struct sigaction sa;
    sa.sa_handler = sig_handler;
    sa.sa_mask = mask;

    // todo: error handling
    sigaction(sig, &sa, nullptr);
}

SigEvent::SigEvent()
{
    // todo: ensure singleton by checking on SIGNAL_FD value

    int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (fd != -1) {
        // todo: logging
        // log::trace!("signal fd: {}", fd);
        SIGNAL_FD.store(fd, std::memory_order_relaxed);
        m_evfd = fd;
    } else {
        // todo: error handlin
    }
}

SigEvent::~SigEvent()
{
    if (m_evfd != -1) {
        close(m_evfd);
    }
}

uint64_t SigEvent::read()
{
    // read the eventfd blob. interrupted or wouldblock result
    // can be ignored (and we go around again), other errors should
    // result in a stop.
    std::array<unsigned char, 8> buf;
    ssize_t res = ::read(m_evfd, &buf, 8);
    if (res == -1) {
        if (errno != EINTR && errno != EWOULDBLOCK) {
            // todo: error handling
            // return Err(e);
            return 0;
        }
    }

    uint64_t current = 0;
    for (;;) {
        // strong to avoid spurious signals
        if (SIGS_PENDING.compare_exchange_strong(
                    current,
                    0,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
            break;
        }
    }
    return current;
}
