#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <cstdint>
#include <csignal>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pty.h>
#include <sys/wait.h>
#include <cctype>
#include <ev++.h>

#include "rwte/config.h"
#include "rwte/rwte.h"
#include "rwte/logging.h"
#include "rwte/term.h"
#include "rwte/tty.h"
#include "rwte/utf8.h"
#include "rwte/window.h"
#include "rwte/luastate.h"

#define LOGGER() (logging::get("tty"))

#define MIN(a, b) ((a) < (b)? (a) : (b))

// most we write in a chunk
const int max_write = 255;

static void log_write(bool initial, const char *data, size_t len)
{
    if (logging::trace < LOGGER()->level())
        return;

    fmt::MemoryWriter msg;
    for (size_t i = 0; i < len; i++)
    {
        char ch = data[i];
        if (ch == '\033')
            msg << "ESC";
        else if (isprint(ch))
            msg << ch;
        else
            msg.write("<{:02x}>", (unsigned int) ch);
    }

    LOGGER()->trace("wrote '{}' ({}, {})", msg.str(), len, initial);
}

// todo: consider merging into print
static ssize_t xwrite(int fd, const char *s, size_t len)
{
    size_t aux = len;
    ssize_t r;

    while (len > 0)
    {
        r = write(fd, s, len);
        if (r < 0)
            return r;
        len -= r;
        s += r;
    }

    return aux;
}

static void setenv_windowid()
{
    char buf[sizeof(long) * 8 + 1];

    snprintf(buf, sizeof(buf), "%u", window.windowid());
    setenv("WINDOWID", buf, 1);
}

static void execsh()
{
    errno = 0;

    const struct passwd *pw;
    if ((pw = getpwuid(getuid())) == NULL) {
        if (errno)
            LOGGER()->fatal("getpwuid failed: {}", strerror(errno));
        else
            LOGGER()->fatal("getpwuid failed for unknown reasons");
    }

    auto L = rwte.lua();
    L->getglobal("config");
    bool sh_on_stack = false;

    // check env first...
    const char *sh = std::getenv("SHELL");
    if (!sh)
    {
        // check value in /etc/passwd
        if (pw->pw_shell[0])
            sh = pw->pw_shell;
        else
        {
            // still couldn't find it. check config
            L->getfield(-1, "default_shell");
            sh = L->tostring(-1);
            if (!sh)
                LOGGER()->fatal("config.default_shell is not valid");
            sh_on_stack = true;
        }
    }

    L->getfield(sh_on_stack? -2 : -1, "term_name");
    const char * term_name = L->tostring(-1);
    if (!term_name)
        LOGGER()->fatal("config.term_name is not valid");

    unsetenv("COLUMNS");
    unsetenv("LINES");
    unsetenv("TERMCAP");
    setenv("LOGNAME", pw->pw_name, 1);
    setenv("USER", pw->pw_name, 1);
    setenv("SHELL", sh, 1);
    setenv("HOME", pw->pw_dir, 1);
    setenv("TERM", term_name, 1);
    setenv_windowid();

    signal(SIGCHLD, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGALRM, SIG_DFL);


    const char *prog;
    if (options.cmd)
        prog = options.cmd[0];
    else
        prog = sh;

    const char **args;
    const char *progags[2] = {
        prog,
        0
    };
    args = (options.cmd) ? options.cmd : progags;

    execvp(prog, const_cast<char* const*>(args));
    LOGGER()->fatal("execvp failed: {}", strerror(errno));

    // yeah, we aren't popping the lua stack, but
    // then, there's no way to get here to do it.
}

static void stty()
{
    // get args from config
    auto L = rwte.lua();
    L->getglobal("config");
    L->getfield(-1, "stty_args");

    char cmd[_POSIX_ARG_MAX];

    size_t len = 0;
    const char * stty_args = L->tolstring(-1, &len);
    if (!stty_args || len == 0 || len > sizeof(cmd)-1)
        LOGGER()->fatal("config.stty_args is invalid");

    memcpy(cmd, stty_args, len);
    L->pop(2);

    char *q = cmd + len;
    size_t siz = sizeof(cmd) - len;
    const char *s;
    for (const char **p = options.cmd; p && (s = *p); ++p)
    {
        if ((len = strlen(s)) > siz-1)
            LOGGER()->fatal("config.stty_args parameter length too long");

        *q++ = ' ';
        memcpy(q, s, len);
        q += len;
        siz -= len + 1;
    }
    *q = '\0';
    if (system(cmd) != 0)
        LOGGER()->fatal("Couldn't call stty");

}

class TtyImpl
{
public:
    TtyImpl();
    ~TtyImpl();

    void resize();

    void write(const char *data, std::size_t len);
    void print(const char *data, std::size_t len);

    void hup();

private:
    void cmdiocb(ev::io &, int);

    void read_ready();
    void write_ready();

    ev::io m_cmdio;

    pid_t m_pid;
    int m_cmdfd;
    int m_iofd;

    // fixed-size read buffer
    std::array<char, BUFSIZ> m_rbuffer;
    std::size_t m_rbuflen;

    // write buffer
    char * m_wbuffer;
    std::size_t m_wbuflen;
};


TtyImpl::TtyImpl() :
    m_pid(0),
    m_cmdfd(-1),
    m_iofd(-1),
    m_rbuflen(0),
    m_wbuffer(nullptr),
    m_wbuflen(0)
{
    m_cmdio.set<TtyImpl,&TtyImpl::cmdiocb>(this);

    if (!options.io.empty())
    {
        LOGGER()->debug("logging to {}", options.io);

        g_term->setprint();
        m_iofd = (options.io == "-") ?
                STDOUT_FILENO : open(options.io.c_str(), O_WRONLY | O_CREAT, 0666);
        if (m_iofd < 0)
        {
            LOGGER()->error("error opening {}: {}",
                options.io, strerror(errno));
            m_iofd = -1;
        }
    }

    if (!options.line.empty())
    {
        LOGGER()->debug("using line {}", options.line);

        if ((m_cmdfd = open(options.line.c_str(), O_RDWR)) < 0)
            LOGGER()->fatal("open line failed: {}", strerror(errno));
        dup2(m_cmdfd, STDIN_FILENO);
        stty();
        return;
    }

    // open pseudoterminal
    int parent, child;
    struct winsize w = {(uint16_t) g_term->rows(), (uint16_t) g_term->cols(), 0, 0};
    if (openpty(&parent, &child, NULL, NULL, &w) < 0)
        LOGGER()->fatal("openpty failed: {}", strerror(errno));

    pid_t pid;
    switch (pid = fork()) {
    case -1: // error
        LOGGER()->fatal("fork failed");
        break;
    case 0: // child
        close(m_iofd);
        setsid(); // create a new process group
        dup2(child, STDIN_FILENO);
        dup2(child, STDOUT_FILENO);
        dup2(child, STDERR_FILENO);
        if (ioctl(child, TIOCSCTTY, NULL) < 0)
            LOGGER()->fatal("ioctl TIOCSCTTY failed: {}", strerror(errno));
        close(child);
        close(parent);
        execsh();
        break;
    default: // parent
        close(child);

        m_pid = pid;
        rwte.watch_child(pid);

        m_cmdfd = parent;
        m_cmdio.start(m_cmdfd, ev::READ);
        break;
    }
}

TtyImpl::~TtyImpl()
{
    if (m_iofd)
        close(m_iofd);
    if (m_cmdfd)
        close(m_cmdfd);

    if (m_wbuffer)
        std::free(m_wbuffer);
}

void TtyImpl::resize()
{
    LOGGER()->info("resize to {}x{}", g_term->cols(), g_term->rows());

    struct winsize w {
        (uint16_t) g_term->rows(),
        (uint16_t) g_term->cols(),
        window.tw(),
        window.th()
    };

    if (ioctl(m_cmdfd, TIOCSWINSZ, &w) < 0)
        LOGGER()->error("could not set window size: {}", strerror(errno));
}

void TtyImpl::write(const char *data, size_t len)
{
    // if nothing's pending for write, kick it off
    if (m_wbuflen == 0)
    {
        ssize_t written = ::write(m_cmdfd, data, MIN(len, max_write));
        if (written < 0)
            written = 0;

        if (written > 0)
            log_write(true, data, written);

        if (written == len)
            return;

        data += written;
        len  -= written;
    }

    // copy anything left into m_wbuffer
    m_wbuffer = (char *) std::realloc(m_wbuffer, m_wbuflen + len);
    std::memcpy(m_wbuffer + m_wbuflen, data, len);
    m_wbuflen += len;

    // now we want write events too
    m_cmdio.set(ev::READ | ev::WRITE);
}

void TtyImpl::print(const char *data, size_t len)
{
    if (m_iofd != -1 && xwrite(m_iofd, data, len) < 0)
    {
        LOGGER()->error("error writing in {}: {}",
                options.io, strerror(errno));
        close(m_iofd);
        m_iofd = -1;
    }
}

void TtyImpl::hup()
{
    // send SIGHUP to shell
    kill(m_pid, SIGHUP);
}

void TtyImpl::cmdiocb(ev::io &, int revents)
{
    if (revents & ev::READ)
        read_ready();

    if (revents & ev::WRITE)
        write_ready();
}


void TtyImpl::read_ready()
{
    char *ptr = &m_rbuffer[0];
    int charsize; // size of utf8 char in bytes
    Rune unicodep;

    // append read bytes to unprocessed bytes
    int ret;
    if ((ret = ::read(m_cmdfd, ptr+m_rbuflen, m_rbuffer.size()-m_rbuflen)) < 0)
    {
        if (errno == EIO)
        {
            // child exiting?
            m_cmdio.stop();
            return;
        }
        else
            LOGGER()->fatal("could not read from shell ({}): {}", errno, strerror(errno));
    }

    m_rbuflen += ret;

    for (;;)
    {
        // UTF8 but not SIXEL
        if (g_term->mode()[MODE_UTF8] && !g_term->mode()[MODE_SIXEL])
        {
            // process a complete utf8 char
            charsize = utf8decode(ptr, &unicodep, m_rbuflen);
            if (charsize == 0)
                break; // incomplete char

            g_term->putc(unicodep);
            ptr += charsize;
            m_rbuflen -= charsize;
        }
        else
        {
            if (m_rbuflen <= 0)
                break;

            g_term->putc(*ptr++ & 0xFF);
            m_rbuflen--;
        }
    }

    // keep any uncomplete utf8 char for the next call
    if (m_rbuflen > 0)
        std::memmove(&m_rbuffer[0], ptr, m_rbuflen);
}

void TtyImpl::write_ready()
{
    int written = ::write(m_cmdfd, m_wbuffer, MIN(m_wbuflen, max_write));
    if (written > 0)
    {
        log_write(false, m_wbuffer, written);
        m_wbuflen -= written;

        // anything left to write?
        if (m_wbuflen)
        {
            // nope.
            std::free(m_wbuffer);
            m_wbuffer = nullptr;

            // stop waiting for write events
            m_cmdio.set(ev::READ);
            return;
        }
        else
        {
            // if anything's left, move it up front
            std::memmove(m_wbuffer, m_wbuffer + written, m_wbuflen);
        }
    }
    else if (written != -1 || (errno != EAGAIN && errno != EINTR))
    {
        // todo: better error handling
        // for now, just stop waiting for write event
        m_cmdio.set(ev::READ);
    }
}

Tty::Tty() :
    impl(std::make_unique<TtyImpl>())
{ }

Tty::~Tty()
{ }

void Tty::resize()
{ impl->resize(); }

void Tty::write(const std::string& data)
{ impl->write(data.c_str(), data.size()); }

void Tty::write(const char *data, std::size_t len)
{ impl->write(data, len); }

void Tty::print(const char *data, std::size_t len)
{ impl->print(data, len); }

void Tty::hup()
{ impl->hup(); }
