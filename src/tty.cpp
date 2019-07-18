#include "rwte/asyncio.h"
#include "lua/state.h"
#include "rwte/config.h"
#include "rwte/logging.h"
#include "rwte/rwte.h"
#include "rwte/term.h"
#include "rwte/tty.h"
#include "rwte/utf8.h"
#include "rwte/window.h"

#include <cctype>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <ev++.h>
#include <fcntl.h>
#include <limits.h>
#include <pty.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define LOGGER() (logging::get("tty"))

// todo: std::min
#define MIN(a, b) ((a) < (b)? (a) : (b))

// most we write in a chunk
const std::size_t max_write = 255;

static void setenv_windowid(Window *window)
{
    // todo: don't set this for wayland

    char buf[sizeof(long) * 8 + 1];

    snprintf(buf, sizeof(buf), "%u", window->windowid());
    setenv("WINDOWID", buf, 1);
}

static void execsh(Window *window)
{
    errno = 0;

    const struct passwd *pw;
    if ((pw = getpwuid(getuid())) == nullptr)
    {
        if (errno)
            LOGGER()->fatal("getpwuid failed: {}", strerror(errno));
        else
            LOGGER()->fatal("getpwuid failed for unknown reasons");
    }

    auto L = rwte->lua();
    L->getglobal("config");
    bool sh_on_stack = false;

    // check options for shell; use options.cmd
    // if set, otherwise try fallbacks
    std::vector<const char *> args = options.cmd;
    if (args.empty())
    {
        // check env next...
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

        args.push_back(sh);
    }

    // needs a null sentinel
    args.push_back(nullptr);

    L->getfield(sh_on_stack? -2 : -1, "term_name");
    const char * term_name = L->tostring(-1);
    if (!term_name)
        LOGGER()->fatal("config.term_name is not valid");

    unsetenv("COLUMNS");
    unsetenv("LINES");
    unsetenv("TERMCAP");
    setenv("LOGNAME", pw->pw_name, 1);
    setenv("USER", pw->pw_name, 1);
    setenv("SHELL", args[0], 1);
    setenv("HOME", pw->pw_dir, 1);
    setenv("TERM", term_name, 1);
    setenv_windowid(window);

    signal(SIGCHLD, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGALRM, SIG_DFL);

    execvp(args[0], const_cast<char* const*>(args.data()));
    LOGGER()->fatal("execvp failed: {}", strerror(errno));

    // yeah, we aren't popping the lua stack, but
    // then, there's no way to get here to do it.
}

static void stty()
{
    // get args from config
    auto L = rwte->lua();
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
    for (const char **p = options.cmd.data(); p && (s = *p); ++p)
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

class TtyImpl: public AsyncIO<TtyImpl, max_write>
{
public:
    TtyImpl(std::shared_ptr<event::Bus> bus, std::shared_ptr<term::Term> term);
    ~TtyImpl();

    void open(Window *window);

    void print(const char *data, std::size_t len);

    void hup();

private:
    void onresize(const event::Resize& evt);

    friend class AsyncIO<TtyImpl, max_write>;
    void log_write(bool initial, const char *data, size_t len);
    std::size_t onread(const char *ptr, std::size_t len);

    std::shared_ptr<event::Bus> m_bus;
    std::shared_ptr<term::Term> m_term;
    int m_resizeReg;
    pid_t m_pid;
    int m_iofd;
};


TtyImpl::TtyImpl(std::shared_ptr<event::Bus> bus,
        std::shared_ptr<term::Term> term) :
    m_bus(std::move(bus)),
    m_term(std::move(term)),
    m_resizeReg(m_bus->reg<event::Resize, TtyImpl, &TtyImpl::onresize>(this)),
    m_pid(0),
    m_iofd(-1)
{
    if (!options.io.empty())
    {
        LOGGER()->debug("logging to {}", options.io);

        m_term->setprint();
        m_iofd = (options.io == "-") ?
                STDOUT_FILENO : ::open(options.io.c_str(), O_WRONLY | O_CREAT, 0666);
        if (m_iofd < 0)
        {
            LOGGER()->error("error opening {}: {}",
                options.io, strerror(errno));
            m_iofd = -1;
        }
    }
}

TtyImpl::~TtyImpl()
{
    m_bus->unreg<event::Resize>(m_resizeReg);

    if (m_iofd != -1)
        close(m_iofd);
}

void TtyImpl::open(Window *window)
{
    if (!options.line.empty())
    {
        LOGGER()->debug("using line {}", options.line);

        int fd;
        if ((fd = ::open(options.line.c_str(), O_RDWR)) < 0)
            LOGGER()->fatal("open line failed: {}", strerror(errno));
        dup2(fd, STDIN_FILENO);
        setFd(fd);

        stty();
        return;
    }

    // open pseudoterminal
    int parent, child;
    struct winsize w = {
        (uint16_t) m_term->rows(),
        (uint16_t) m_term->cols(),
        0, 0
    };
    if (openpty(&parent, &child, nullptr, nullptr, &w) < 0)
        LOGGER()->fatal("openpty failed: {}", strerror(errno));

    pid_t pid;
    switch (pid = fork()) {
    case -1: // error
        LOGGER()->fatal("fork failed");
        break;
    case 0: // child
        if (m_iofd != -1)
            close(m_iofd);
        setsid(); // create a new process group
        dup2(child, STDIN_FILENO);
        dup2(child, STDOUT_FILENO);
        dup2(child, STDERR_FILENO);
        if (ioctl(child, TIOCSCTTY, nullptr) < 0)
            LOGGER()->fatal("ioctl TIOCSCTTY failed: {}", strerror(errno));
        close(child);
        close(parent);
        execsh(window);
        break;
    default: // parent
        close(child);

        m_pid = pid;
        rwte->watch_child(pid);

        setFd(parent);
        startRead();
        break;
    }
}

void TtyImpl::print(const char *data, size_t len)
{
    if (m_iofd != -1 && len > 0)
    {
        ssize_t r;
        while (len > 0)
        {
            r = ::write(m_iofd, data, len);
            if (r < 0)
            {
                LOGGER()->error("error writing in {}: {}",
                        options.io, strerror(errno));
                close(m_iofd);
                m_iofd = -1;
                break;
            }

            len -= r;
            data += r;
        }
    }
}

void TtyImpl::hup()
{
    // send SIGHUP to shell
    kill(m_pid, SIGHUP);
}

void TtyImpl::onresize(const event::Resize& evt)
{
    LOGGER()->info("resize to {}x{}", evt.cols, evt.rows);

    struct winsize w {
        (uint16_t) evt.rows,
        (uint16_t) evt.cols,
        // unused
        0, 0
    };

    if (ioctl(fd(), TIOCSWINSZ, &w) < 0)
        LOGGER()->error("could not set window size: {}", strerror(errno));
}

void TtyImpl::log_write(bool initial, const char *data, size_t len)
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

std::size_t TtyImpl::onread(const char *ptr, std::size_t len)
{
    for (;;)
    {
        // UTF8 but not SIXEL
        if (m_term->mode()[term::MODE_UTF8] &&
                !m_term->mode()[term::MODE_SIXEL])
        {
            // process a complete utf8 char
            char32_t unicodep;
            int charsize = utf8decode(ptr, &unicodep, len);
            if (charsize == 0)
                break; // incomplete char

            m_term->putc(unicodep);
            ptr += charsize;
            len -= charsize;
        }
        else
        {
            if (len <= 0)
                break;

            m_term->putc(*ptr++ & 0xFF);
            len--;
        }
    }

    return len;
}

Tty::Tty(std::shared_ptr<event::Bus> bus, std::shared_ptr<term::Term> term) :
    impl(std::make_unique<TtyImpl>(std::move(bus), std::move(term)))
{ }

Tty::~Tty()
{ }

void Tty::open(Window *window)
{ impl->open(window); }

void Tty::write(const std::string& data)
{ impl->write(data.c_str(), data.size()); }

void Tty::write(const char *data, std::size_t len)
{ impl->write(data, len); }

void Tty::print(const char *data, std::size_t len)
{ impl->print(data, len); }

void Tty::hup()
{ impl->hup(); }
