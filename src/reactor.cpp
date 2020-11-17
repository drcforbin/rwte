#include "rw/logging.h"
#include "rwte/reactor.h"
#include "rwte/sigevent.h"

#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#define LOGGER() (rw::logging::get("reactor"))

constexpr timespec to_timespec(float secs)
{
    const long ms = secs * 1000;
    return {
            .tv_sec = ms / 1000,
            .tv_nsec = (ms % 1000) * 1000000};
}

namespace reactor {

ReactorError::ReactorError(const std::string& arg) :
    std::runtime_error(arg)
{}

ReactorError::ReactorError(const char* arg) :
    std::runtime_error(arg)
{}

ReactorError::~ReactorError()
{}

Reactor::Reactor()
{
    m_epfd = epoll_create1(EPOLL_CLOEXEC);
    if (m_epfd != -1) {
        reg_fd(m_sig.fd());

        // todo: improve error handling...which failed?
        connect_handler(SIGTERM);
        connect_handler(SIGINT);
        connect_handler(SIGHUP);
        connect_handler(SIGCHLD);
    } else {
        throw ReactorError(fmt::format("could not create epoll ({}): {}",
                errno, strerror(errno)));
    }
}

Reactor::~Reactor()
{
    // note: not checking the return values from these
    // close calls. none of these are real files, and
    // the linux docs for close imply that it should not
    // be retried.

    if (m_epfd != -1) {
        close(m_epfd);
    }

    if (m_refreshfd != -1) {
        close(m_refreshfd);
    }

    if (m_repeatfd != -1) {
        close(m_repeatfd);
    }

    if (m_blinkfd != -1) {
        close(m_blinkfd);
    }
}

void Reactor::set_ttyfd(int ttyfd)
{
    reg_fd(ttyfd);
    m_ttyfd = ttyfd;
}

void Reactor::set_windowfd(int windowfd)
{
    reg_fd(windowfd);
    m_windowfd = windowfd;
}

void Reactor::set_write(int fd, bool write)
{
    auto evts = EPOLLIN | (write ? EPOLLOUT : 0);
    epoll_event ev{.events = evts, .data = {.fd = fd}};

    if (epoll_ctl(m_epfd, EPOLL_CTL_MOD, fd, &ev) == -1) {
        if (errno == EINVAL) {
            throw ReactorError(fmt::format("unable to set events for fd {}: bad arg", fd));
        } else {
            throw ReactorError(fmt::format("unable to set events for fd {}, ({}): {}",
                    fd, errno, strerror(errno)));
        }
    }
}

void Reactor::unreg(int fd) {
    // ev only being passed for compatibility
    epoll_event ev{};
    if (epoll_ctl(m_epfd, EPOLL_CTL_DEL, fd, &ev) == -1) {
        if (errno == EINVAL) {
            throw ReactorError(fmt::format("unable to unregister fd {}: bad arg", fd));
        } else {
            throw ReactorError(fmt::format("unable to unregister fd {}, ({}): {}",
                    fd, errno, strerror(errno)));
        }
    }
}

void Reactor::queue_refresh(float secs)
{
    if (m_refreshfd == -1) {
        m_refreshfd = make_timer();
    }
    set_timer(m_refreshfd, secs, 0);
}

void Reactor::start_repeat(float secs)
{
    if (m_repeatfd == -1) {
        m_repeatfd = make_timer();
    }
    set_timer(m_repeatfd, secs, secs);
}

void Reactor::stop_repeat()
{
    if (m_repeatfd != -1) {
        set_timer(m_repeatfd, 0, 0);
    }
}

void Reactor::start_blink(float secs)
{
    if (m_blinkfd == -1) {
        m_blinkfd = make_timer();
    }
    set_timer(m_blinkfd, secs, secs);
}

void Reactor::stop_blink()
{
    if (m_blinkfd != -1) {
        set_timer(m_blinkfd, 0, 0);
    }
}

int clear_timer(int fd) {
    uint64_t exp = 0;
    int res = read(fd, &exp, sizeof(uint64_t));
    if (res != sizeof(uint64_t)) {
        LOGGER()->fatal("unexpected result timer read {}, ({}): {}",
                fd, errno, strerror(errno));
    }
    return exp;
}

Event Reactor::wait()
{
    // todo: consider something like a drain function?

    // check queue first
    if (!m_queue.empty()) {
        auto evt = m_queue.front();
        m_queue.pop_front();
        return evt;
    }

    // prealloc space for events
    epoll_event events[5] = {};

    for (;;) {
        auto cnt = epoll_wait(m_epfd, events, 4, -1);

        // if >0, it's a count, if ==0, unexpected timeout
        for (auto i = 0; i < cnt; i++) {
            if (events[i].data.fd == m_ttyfd) {
                if (events[i].events == (EPOLLIN | EPOLLOUT)) {
                    enqueue(TtyRead{});
                    return TtyWrite{};
                } else if (events[i].events == EPOLLIN) {
                    return TtyRead{};
                } else if (events[i].events == EPOLLOUT) {
                    return TtyWrite{};
                } else if (events[i].events == EPOLLHUP) {
                    // todo: how to handle EPOLLHUP?
                    continue;
                } else if (events[i].events == EPOLLERR) {
                    // todo: how to handle EPOLLERR?
                    continue;
                }

                auto event = events[i].events;
                LOGGER()->warn(fmt::format("unexpected tty event ({})",
                            event));
                continue;
            } else if (events[i].data.fd == m_windowfd) {
                return Window{};
            } else if (events[i].data.fd == m_sig.fd()) {
                // if we have a sig, try to read from it and return
                if (auto mask = m_sig.read()) {
                    std::optional<Event> first;

                    while (mask != 0) {
                        Event evt = Stop{};

                        // count # of trailing zeroes
                        auto t = __builtin_ctzl(mask);
                        switch (t) {
                            case SIGCHLD:
                                evt = ChildEnd{};
                                break;
                            case SIGTERM:
                                [[fallthrough]];
                            case SIGINT:
                                [[fallthrough]];
                            case SIGHUP:
                                break;
                            default:
                                LOGGER()->error("received an unexpected signal {}", t);
                                break;
                        }

                        if (!first.has_value()) {
                            first = evt;
                        } else {
                            enqueue(evt);
                        }

                        // unset least significant bit
                        mask ^= mask & (-mask);
                    }

                    if (first.has_value()) {
                        return first.value();
                    }
                }

                LOGGER()->warn("spurious signal event");
                continue;
            } else if (m_refreshfd != -1 && events[i].data.fd == m_refreshfd) {
                clear_timer(m_refreshfd);
                return Refresh{};
            } else if (m_repeatfd != -1 && events[i].data.fd == m_repeatfd) {
                int exp = clear_timer(m_repeatfd);

                // if we missed some key repeats, queue them
                for (; exp > 1; exp--) {
                    enqueue(RepeatKey{});
                }

                return RepeatKey{};
            } else if (m_blinkfd != -1 && events[i].data.fd == m_blinkfd) {
                clear_timer(m_blinkfd);
                return Blink{};
            }

            LOGGER()->error("received an unexpected fd {}", events[i].data.fd);
        }

        if (cnt == 0) {
            throw ReactorError("epoll unexpectedly timed out");
        } else if (cnt == -1 && errno != EINTR) {
            throw ReactorError(fmt::format("epoll failed, ({}): {}",
                    errno, strerror(errno)));
        }
    }
}

void Reactor::enqueue(Event evt)
{
    m_queue.push_back(evt);
}

int Reactor::make_timer() {
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if (fd != -1) {
        reg_fd(fd);
    } else {
        throw ReactorError(fmt::format("unable to crate timerfd ({}): {}",
                fd, errno, strerror(errno)));
    }
    return fd;
}

void Reactor::set_timer(int fd, float initial_secs, float repeat_secs) {
    struct itimerspec ts
    {
        .it_interval = to_timespec(repeat_secs),
        .it_value = to_timespec(initial_secs)
    };

    if (timerfd_settime(fd, 0, &ts, nullptr) == -1) {
        if (errno == EINVAL) {
            throw ReactorError(fmt::format("unable to set time fd {}: bad arg", fd));
        } else {
            throw ReactorError(fmt::format("unable to set timer fd {}, ({}): {}",
                    fd, errno, strerror(errno)));
        }
    }
}

void Reactor::reg_fd(int fd)
{
    // all of the fds we register should initially listen for readable
    epoll_event ev{.events = EPOLLIN, .data = {.fd = fd}};

    if (epoll_ctl(m_epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        if (errno == EINVAL) {
            throw ReactorError(fmt::format("unable to register fd {}: bad arg", fd));
        } else {
            throw ReactorError(fmt::format("unable to register fd {}, ({}): {}",
                    fd, errno, strerror(errno)));
        }
    }
}

} // namespace reactor
