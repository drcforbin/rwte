#ifndef RWTE_REACTOR_H
#define RWTE_REACTOR_H

// todo: reorg includes, switch to reactor pimpl?

#include "rwte/sigevent.h"
#include "rwte/reactorctrl.h"

#include <deque>
#include <optional>
#include <variant>

namespace reactor {

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
    void set_events(int fd, bool read, bool write);

    void queue_refresh(float secs);
    void start_repeat(float secs);
    void stop_repeat();
    void start_blink(float secs);
    void stop_blink();

    Event wait();

    void enqueue(Event evt);
    void stop();

private:
    void reg_fd(int fd, bool read, bool write);

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
