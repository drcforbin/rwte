#include "rw/logging.h"
#include "rwte/sigevent.h"

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <errno.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define LOGGER() (rw::logging::get("sigevent"))

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

// todo: de-genericify this or move it to librw...we dont need general purpose
void connect_handler(int sig)
{
    // we only have space for 64 signals...
    if (sig > 64) {
        throw SigEventError(fmt::format("requested sig {}, only 64 signals are supported", sig));
    }

    sigset_t mask;
    sigfillset(&mask);

    // todo: do we want SA_NOCLDSTOP / SA_NOCLDWAIT when SIGCHLD,
    // we're checking on whether it indicates an exit in the tty
    struct sigaction sa;
    sa.sa_handler = sig_handler;
    sa.sa_mask = mask;

    if (sigaction(sig, &sa, nullptr) == -1) {
        throw SigEventError(fmt::format("unable to register signal {}, ({}): {}",
                sig, errno, strerror(errno)));
    }
}

SigEventError::SigEventError(const std::string& arg) :
    std::runtime_error(arg)
{}

SigEventError::SigEventError(const char* arg) :
    std::runtime_error(arg)
{}

SigEventError::~SigEventError()
{}

SigEvent::SigEvent()
{
    // todo: ensure singleton by checking on SIGNAL_FD value

    int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (fd != -1) {
        LOGGER()->trace("signal fd: {}", fd);
        SIGNAL_FD.store(fd, std::memory_order_relaxed);
        m_evfd = fd;
    } else {
        throw SigEventError(fmt::format("unable to create signal fd ({}): {}",
                errno, strerror(errno)));
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
            LOGGER()->fatal("unable to read signal fd {}, ({}): {}",
                    m_evfd, errno, strerror(errno));
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
