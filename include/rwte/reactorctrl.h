#ifndef RWTE_REACTORCTRL_H
#define RWTE_REACTORCTRL_H

namespace reactor {

class ReactorCtrl
{
public:
    virtual ~ReactorCtrl() {}

    virtual void set_events(int fd, bool read, bool write) = 0;

    virtual void queue_refresh(float secs) = 0;
    virtual void start_repeat(float secs) = 0;
    virtual void stop_repeat() = 0;
    virtual void start_blink(float secs) = 0;
    virtual void stop_blink() = 0;
};

} // namespace reactor

#endif // RWTE_REACTORCTRL_H
