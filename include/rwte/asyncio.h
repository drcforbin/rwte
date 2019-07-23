#ifndef RWTE_ASYNCIO_H
#define RWTE_ASYNCIO_H

#include <unistd.h>
#include <cstring>
#include <ev++.h>
#include <algorithm>
#include <array>
#include <vector>

template <class T, std::size_t max_write>
class AsyncIO
{
public:
    AsyncIO() :
        m_fd(-1),
        m_rbuflen(0)
    {
        m_io.set<AsyncIO, &AsyncIO::iocb>(this);
    }

    ~AsyncIO()
    {
        if (m_fd != -1)
            close(m_fd);
    }

    void setFd(int fd) { m_fd = fd; }
    int fd() const { return m_fd; }

    void write(const char *data, std::size_t len)
    {
        // if nothing's pending for write, kick it off
        if (m_wbuffer.empty())
        {
            ssize_t written = ::write(m_fd, data, std::min(len, max_write));
            if (written < 0)
                written = 0;

            if (written > 0)
                static_cast<T*>(this)->log_write(true, data, written);

            if (static_cast<std::size_t>(written) == len)
                return;

            data += written;
            len  -= written;
        }

        // copy anything left into m_wbuffer
        std::copy(data, data+len, std::back_inserter(m_wbuffer));

        // now we want write events too
        m_io.set(ev::READ | ev::WRITE);
    }

    void startRead()
    {
        m_io.start(m_fd, ev::READ);
    }

private:
    void iocb(ev::io &, int revents)
    {
        if (revents & ev::READ)
            read_ready();

        if (revents & ev::WRITE)
            write_ready();
    }

    void read_ready()
    {
        char *ptr = &m_rbuffer[0];

        // append read bytes to unprocessed bytes
        int ret;
        if ((ret = ::read(m_fd, ptr+m_rbuflen, m_rbuffer.size()-m_rbuflen)) < 0)
        {
            if (errno == EIO)
            {
                // child exiting?
                m_io.stop();
                return;
            }
            // todo: logging in here
            //else
            //    LOGGER()->fatal("could not read from shell ({}): {}", errno, strerror(errno));
        }

        m_rbuflen += ret;

        m_rbuflen = static_cast<T*>(this)->onread(ptr, m_rbuflen);

        // keep any uncomplete utf8 char for the next call
        if (m_rbuflen > 0)
            std::memmove(&m_rbuffer[0], ptr, m_rbuflen);
    }

    void write_ready()
    {
        auto remaining = m_wbuffer.size();
        int written = ::write(m_fd, m_wbuffer.data(), std::min(remaining, max_write));
        if (written > 0)
        {
            static_cast<T*>(this)->log_write(false, m_wbuffer.data(), written);
            remaining -= written;

            // anything left to write?
            if (!remaining)
            {
                // nope.
                m_wbuffer.resize(0);

                // stop waiting for write events
                m_io.set(ev::READ);
                return;
            }
            else
            {
                // if anything's left, move it up front, and shrink
//todo: remove
                std::copy(m_wbuffer.begin() + written, m_wbuffer.end(),
                        m_wbuffer.begin());
                m_wbuffer.resize(remaining);
            }
        }
        else if (written != -1 || (errno != EAGAIN && errno != EINTR))
        {
            // todo: better error handling
            // for now, just stop waiting for write event
            m_io.set(ev::READ);
        }
    }

    int m_fd;
    ev::io m_io;

    // fixed-size read buffer
    // todo: consider replacing with std::vector
    std::array<char, BUFSIZ> m_rbuffer;
    std::size_t m_rbuflen;

    // write buffer
    std::vector<char> m_wbuffer;
};

#endif // RWTE_ASYNCIO_H
