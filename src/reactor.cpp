#include "rwte/reactor.h"
#include "rwte/sigevent.h"

#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

// todo: error handling / exceptions

// todo: verify nonblocking for all the fds, we really don't want to freeze

namespace reactor {

Reactor::Reactor()
{
    m_epfd = epoll_create1(EPOLL_CLOEXEC);
    if (m_epfd != -1) {
        reg_fd(m_sig.fd(), true, false);

        // todo: improve error handling...which failed?
        connect_handler(SIGTERM);
        connect_handler(SIGINT);
        connect_handler(SIGHUP);
        connect_handler(SIGCHLD);
    } else {
        // todo: check errno? throw?
    }
}

Reactor::~Reactor()
{
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
    // todo: improve error handling, throw
    // "unable to add to epoll fd"?
    reg_fd(ttyfd, true, true);
    m_ttyfd = ttyfd;
}

void Reactor::set_windowfd(int windowfd)
{
    // todo: improve error handling, throw
    // "unable to add to epoll fd"?
    reg_fd(windowfd, true, false);
    m_windowfd = windowfd;
}

void Reactor::set_events(int fd, bool read, bool write)
{
    auto evts = (read ? EPOLLIN : 0) | (write ? EPOLLOUT : 0);
    epoll_event ev{.events = evts, .data = {.fd = fd}};

    // todo: error handling
    epoll_ctl(m_epfd, EPOLL_CTL_MOD, fd, &ev);
}

constexpr timespec to_timespec(float secs)
{
    const long ms = secs * 1000;
    return {
            .tv_sec = ms / 1000,
            .tv_nsec = (ms % 1000) * 1000000};
}

void Reactor::queue_refresh(float secs)
{
    if (m_refreshfd == -1) {
        // todo: error handling
        m_refreshfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
        reg_fd(m_refreshfd, true, false);
    }

    struct itimerspec ts
    {
        .it_value = to_timespec(secs)
    };
    // todo: error handling
    timerfd_settime(m_refreshfd, 0, &ts, NULL);
}

void Reactor::start_repeat(float secs)
{
    if (m_repeatfd == -1) {
        // todo: error handling
        m_repeatfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
        reg_fd(m_repeatfd, true, false);
    }

    // todo: error handling
    struct itimerspec ts
    {
        .it_interval = to_timespec(secs),
        .it_value = to_timespec(secs)
    };
    // todo: error handling
    timerfd_settime(m_repeatfd, 0, &ts, NULL);
}

void Reactor::stop_repeat()
{
    if (m_repeatfd == -1) {
        return;
    }

    struct itimerspec ts
    {};
    // todo: error handling
    timerfd_settime(m_repeatfd, 0, &ts, NULL);
}

void Reactor::start_blink(float secs)
{
    if (m_blinkfd == -1) {
        // todo: error handling
        m_blinkfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
        reg_fd(m_blinkfd, true, false);
    }

    struct itimerspec ts
    {
        .it_interval = to_timespec(secs),
        .it_value = to_timespec(secs)
    };
    // todo: error handling
    timerfd_settime(m_blinkfd, 0, &ts, NULL);
}

void Reactor::stop_blink()
{
    if (m_blinkfd == -1) {
        return;
    }

    struct itimerspec ts
    {};
    // todo: error handling
    timerfd_settime(m_blinkfd, 0, &ts, NULL);
}

Event Reactor::wait()
{
    // todo: error handling, resource cleanup

    // todo: when stop set, finish going through queue,
    // something like a drain function?

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
        if (cnt > 0) {
            // if >0, it's a count, if ==0, ignore.
            for (auto i = 0; i < cnt; i++) {
                if (events[i].data.fd == m_ttyfd) {
                    if (events[i].events == (EPOLLIN | EPOLLOUT)) {
                        enqueue(TtyRead{});
                        return TtyWrite{};
                    } else if (events[i].events == EPOLLIN) {
                        return TtyRead{};
                    } else if (events[i].events == EPOLLOUT) {
                        return TtyWrite{};
                    }
                } else if (events[i].data.fd == m_windowfd) {
                    return Window{};
                } else if (events[i].data.fd == m_sig.fd()) {
                    // if we have a sig, try to read from it and return
                    if (auto mask = m_sig.read()) {
                        std::optional<Event> first;

                        while (mask != 0) {
                            auto t = mask & (-mask);
                            Event evt = Stop{};

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
                                    // todo:
                                    // log::error!("received an unexpected fd");
                                    break;
                            }

                            if (!first.has_value()) {
                                first = evt;
                            } else {
                                enqueue(evt);
                            }

                            mask ^= t;
                        }

                        if (first.has_value()) {
                            return first.value();
                        }
                    }
                } else if (m_refreshfd != -1 && events[i].data.fd == m_refreshfd) {
                    uint64_t exp = 0;
                    auto res = read(m_refreshfd, &exp, sizeof(uint64_t));
                    if (res != sizeof(uint64_t)) {
                        // todo: error handling
                    }
                    return Refresh{};
                } else if (m_repeatfd != -1 && events[i].data.fd == m_repeatfd) {
                    uint64_t exp = 0;
                    auto res = read(m_repeatfd, &exp, sizeof(uint64_t));
                    if (res != sizeof(uint64_t)) {
                        // todo: error handling
                    }

                    // if we missed some key repeats, queue them
                    for (; res > 1; res--) {
                        enqueue(RepeatKey{});
                    }

                    return RepeatKey{};
                } else if (m_blinkfd != -1 && events[i].data.fd == m_blinkfd) {
                    uint64_t exp = 0;
                    auto res = read(m_blinkfd, &exp, sizeof(uint64_t));
                    if (res != sizeof(uint64_t)) {
                        // todo: error handling
                    }
                    return Blink{};
                }
            }

            // todo:
            // log::debug !("received an unexpected fd");
        } else if (cnt == -1) {
            if (errno == EINTR) {
                // ignore
            } else {
                // todo: error handling
                // return Some(Err(e));
            }
        }
    }
}

void Reactor::enqueue(Event evt)
{
    m_queue.push_back(evt);
}

void Reactor::stop()
{
    enqueue(Stop{});
}

void Reactor::reg_fd(int fd, bool read, bool write)
{
    auto evts = (read ? EPOLLIN : 0) | (write ? EPOLLOUT : 0);
    epoll_event ev{.events = evts, .data = {.fd = fd}};

    // todo: error handling
    epoll_ctl(m_epfd, EPOLL_CTL_ADD, fd, &ev);
}

} // namespace reactor
