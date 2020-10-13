#ifndef RWTE_SIGEVENT_H
#define RWTE_SIGEVENT_H

#include <cstdint>

class SigEventError : public std::runtime_error
{
public:
    explicit SigEventError(const std::string& arg);
    explicit SigEventError(const char* arg);

    SigEventError(const SigEventError&) = default;
    SigEventError& operator=(const SigEventError&) = default;
    SigEventError(SigEventError&&) = default;
    SigEventError& operator=(SigEventError&&) = default;

    virtual ~SigEventError();
};

// listens to signals, returns them from read
class SigEvent
{
public:
    SigEvent();
    ~SigEvent();

    int fd() const { return m_evfd; }

    uint64_t read();

private:
    int m_evfd = -1;
};

// todo: move into namespace?
void connect_handler(int sig);

#endif // RWTE_SIGEVENT_H
