#ifndef RWTE_ASYNCIO_H
#define RWTE_ASYNCIO_H

#include "rw/logging.h"
#include "rwte/reactorctrl.h"

#include <array>
#include <unistd.h>
#include <vector>

// capture LOGGER if already set (someone defined it before
// including this file), so it can be restored later
#ifdef LOGGER
#define ASYNCIO_OLD_LOGGER LOGGER
#endif
// define our own logger, just for this header
#define LOGGER() (rw::logging::get("aio"))

template <class T, std::size_t max_write>
class AsyncIO
{
public:
    AsyncIO(reactor::ReactorCtrl *ctrl) :
        m_ctrl(ctrl),
        m_fd(-1),
        m_rbuflen(0)
    {
    }

    ~AsyncIO()
    {
        if (m_fd != -1)
            close(m_fd);
    }

    void setFd(int fd) { m_fd = fd; }
    int fd() const { return m_fd; }

    void write(std::string_view data)
    {
        auto pdata = data.data();
        std::size_t len = data.size();

        // don't write if we don't need to
        if (!len) {
            return;
        }

        // if nothing's pending for write, kick it off
        if (m_wbuffer.empty()) {
            ssize_t written = ::write(m_fd, pdata, std::min(len, max_write));
            // todo: error handling
            // todo: consider throwing
            if (written < 0)
                written = 0;

            if (written > 0)
                static_cast<T*>(this)->log_write(true, pdata, written);

            if (static_cast<std::size_t>(written) == len)
                return;

            pdata += written;
            len -= written;
        }

        // copy anything left into m_wbuffer
        std::copy_n(pdata, len, std::back_inserter(m_wbuffer));

        // now we want write events too
        m_ctrl->set_write(m_fd, true);
    }

    void read_ready()
    {
        char* ptr = &m_rbuffer[0];

        // append read bytes to unprocessed bytes
        int ret;
        if ((ret = ::read(m_fd, ptr + m_rbuflen, m_rbuffer.size() - m_rbuflen)) > 0) {
            m_rbuflen += ret;

            m_rbuflen = static_cast<T*>(this)->onread(ptr, m_rbuflen);

            // todo: this REALLY looks like it should be ptr + ret; test?
            // keep any uncomplete utf8 char for the next call
            if (m_rbuflen > 0)
                std::memmove(&m_rbuffer[0], ptr, m_rbuflen);
        } else if (ret < 0) {
            if (errno == EIO) {
                // child exiting?
                m_ctrl->unreg(m_fd);
            } else if (errno != EAGAIN && errno != EINTR) {
                // ignoring EAGAIN and EINTR (we'll catch them on next
                // ready event), log error
                LOGGER()->fatal("could not read from shell ({}): {}",
                        errno, strerror(errno));
                // todo: consider throwing
            }
        } else {
            // for some reason, read returned zero...this is probably
            // a logic bug somewhere
            LOGGER()->warn("read zero bytes");
        }
    }

    void write_ready()
    {
        auto remaining = m_wbuffer.size();
        int written = ::write(m_fd, m_wbuffer.data(), std::min(remaining, max_write));
        if (written > 0) {
            static_cast<T*>(this)->log_write(false, m_wbuffer.data(), written);
            remaining -= written;

            // anything left to write?
            if (!remaining) {
                // nope.
                m_wbuffer.resize(0);

                // stop waiting for write events
                m_ctrl->set_write(m_fd, false);
            } else {
                // if anything's left, move it up front, and shrink
                //todo: remove
                std::copy(m_wbuffer.begin() + written, m_wbuffer.end(),
                        m_wbuffer.begin());
                m_wbuffer.resize(remaining);
            }
        } else if (written == 0) {
            // this is fine, not really an error. probably means we did
            // something odd, like a zero byte write, or that we're listening
            // for writable when we don't need to
            LOGGER()->warn("write zero bytes");
            m_ctrl->set_write(m_fd, false);
        } else if (errno != EAGAIN && errno != EINTR) {
            // for now, log and stop listening for writable
            LOGGER()->error("write error: {}", strerror(errno));
            m_ctrl->set_write(m_fd, false);
            // todo: consider throwing
        }
    }

private:
    reactor::ReactorCtrl *m_ctrl;

    int m_fd;

    // fixed-size read buffer
    // todo: consider replacing with std::vector
    std::array<char, BUFSIZ> m_rbuffer;
    std::size_t m_rbuflen;

    // write buffer
    std::vector<char> m_wbuffer;
};

// undefine LOGGER, restoring if needed
#undef LOGGER
#ifdef ASYNCIO_OLD_LOGGER
#define LOGGER ASYNCIO_OLD_LOGGER
#undef ASYNCIO_OLD_LOGGER
#endif

#endif // RWTE_ASYNCIO_H
