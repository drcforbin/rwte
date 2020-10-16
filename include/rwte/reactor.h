#ifndef RWTE_REACTOR_H
#define RWTE_REACTOR_H

// todo: reorg includes, switch to reactor pimpl?

#include "rwte/sigevent.h"
#include "rwte/reactorctrl.h"

#include <deque>
#include <optional>
#include <variant>

namespace reactor {

class ReactorError : public std::runtime_error
{
public:
    explicit ReactorError(const std::string& arg);
    explicit ReactorError(const char* arg);

    ReactorError(const ReactorError&) = default;
    ReactorError& operator=(const ReactorError&) = default;
    ReactorError(ReactorError&&) = default;
    ReactorError& operator=(ReactorError&&) = default;

    virtual ~ReactorError();
};

struct TtyRead
{};
struct TtyWrite
{};
struct Window
{};
struct Refresh
{};
struct RepeatKey
{};
struct Blink
{};
struct ChildEnd
{};
struct Stop
{};

using Event = std::variant<
        TtyRead,
        TtyWrite,
        Window,
        Refresh,
        RepeatKey,
        Blink,
        ChildEnd,
        Stop>;

class Reactor : public ReactorCtrl
{
public:
    Reactor();
    ~Reactor();

    void set_ttyfd(int ttyfd);
    void set_windowfd(int windowfd);

    void set_write(int fd, bool write);
    void unreg(int fd);

    void queue_refresh(float secs);
    void start_repeat(float secs);
    void stop_repeat();
    void start_blink(float secs);
    void stop_blink();

    Event wait();

    void enqueue(Event evt);

private:
    int make_timer();
    void set_timer(int fd, float initial_secs, float repeat_secs);
    void reg_fd(int fd);

    int m_epfd = -1;

    int m_refreshfd = -1;
    int m_repeatfd = -1;
    int m_blinkfd = -1;
    int m_ttyfd = -1;
    int m_windowfd = -1;

    SigEvent m_sig;
    std::deque<Event> m_queue;
};

} // namespace reactor

#endif // RWTE_REACTOR_H
