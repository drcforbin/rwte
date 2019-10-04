#ifndef RWTE_LOGGING_H
#define RWTE_LOGGING_H

#include "fmt/format.h"

#include <chrono>
#include <memory>
#include <string>

namespace logging {

enum level_enum
{
    trace = 0,
    debug = 1,
    info = 2,
    warn = 3,
    err = 4,
    fatal = 5,
    off = 6
};

class Logger
{
public:
    Logger(std::string_view name) :
        m_name(name),
        m_level(logging::trace)
    {}

    virtual ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string_view name() const { return m_name; }

    logging::level_enum level() { return m_level; }
    void level(logging::level_enum val) { m_level = val; }

    template <typename... Args>
    void log(level_enum lvl, std::string_view fmt, const Args&... args);
    template <typename... Args>
    void log(level_enum lvl, std::string_view msg);
    template <typename Arg1, typename... Args>
    void trace(std::string_view fmt, const Arg1&, const Args&... args);
    template <typename Arg1, typename... Args>
    void debug(std::string_view fmt, const Arg1&, const Args&... args);
    template <typename Arg1, typename... Args>
    void info(std::string_view fmt, const Arg1&, const Args&... args);
    template <typename Arg1, typename... Args>
    void warn(std::string_view fmt, const Arg1&, const Args&... args);
    template <typename Arg1, typename... Args>
    void error(std::string_view fmt, const Arg1&, const Args&... args);
    template <typename Arg1, typename... Args>
    void fatal(std::string_view fmt, const Arg1&, const Args&... args);

    template <typename T>
    void log(level_enum lvl, const T&);
    template <typename T>
    void trace(const T&);
    template <typename T>
    void debug(const T&);
    template <typename T>
    void info(const T&);
    template <typename T>
    void warn(const T&);
    template <typename T>
    void error(const T&);
    template <typename T>
    void fatal(const T&);

private:
    const std::string m_name;
    logging::level_enum m_level;
};

// get/create a logger
std::shared_ptr<Logger> get(std::string_view name);

// get/create a logger for debugging
std::shared_ptr<Logger> dbg();

namespace details {
struct Message
{
    Message() = default;
    Message(std::string_view logname, logging::level_enum lvl) :
        logname(logname),
        level(lvl),
        ts(std::chrono::system_clock::now())
    {}

    Message(const Message& other) = delete;
    Message& operator=(Message&& other) = delete;
    Message(Message&& other) = delete;

    std::string_view logname;
    logging::level_enum level = logging::trace;
    std::chrono::system_clock::time_point ts;

    std::string msg;
};

void log_message(const Message& msg);

} // namespace details
} // namespace logging

// impl bits:

inline logging::Logger::~Logger() = default;

template <typename... Args>
inline void logging::Logger::log(logging::level_enum lvl, std::string_view fmt, const Args&... args)
{
    if (lvl < m_level)
        return;

    details::Message message(m_name, lvl);
    message.msg = fmt::format(fmt, args...);
    details::log_message(message);

    if (lvl == logging::fatal)
        exit(1);
}

template <typename... Args>
inline void logging::Logger::log(logging::level_enum lvl, std::string_view msg)
{
    if (lvl < m_level)
        return;

    details::Message message(m_name, lvl);
    message.msg = msg;
    details::log_message(message);

    if (lvl == logging::fatal)
        exit(1);
}

template <typename T>
inline void logging::Logger::log(logging::level_enum lvl, const T& msg)
{
    if (lvl < m_level)
        return;

    details::Message message(m_name, lvl);
    message.msg = fmt::format(msg);
    details::log_message(message);

    if (lvl == logging::fatal)
        exit(1);
}

template <typename Arg1, typename... Args>
inline void logging::Logger::trace(std::string_view fmt, const Arg1& arg1, const Args&... args)
{
    log(logging::trace, fmt, arg1, args...);
}

template <typename Arg1, typename... Args>
inline void logging::Logger::debug(std::string_view fmt, const Arg1& arg1, const Args&... args)
{
    log(logging::debug, fmt, arg1, args...);
}

template <typename Arg1, typename... Args>
inline void logging::Logger::info(std::string_view fmt, const Arg1& arg1, const Args&... args)
{
    log(logging::info, fmt, arg1, args...);
}

template <typename Arg1, typename... Args>
inline void logging::Logger::warn(std::string_view fmt, const Arg1& arg1, const Args&... args)
{
    log(logging::warn, fmt, arg1, args...);
}

template <typename Arg1, typename... Args>
inline void logging::Logger::error(std::string_view fmt, const Arg1& arg1, const Args&... args)
{
    log(logging::err, fmt, arg1, args...);
}

template <typename Arg1, typename... Args>
inline void logging::Logger::fatal(std::string_view fmt, const Arg1& arg1, const Args&... args)
{
    log(logging::fatal, fmt, arg1, args...);
}

template <typename T>
inline void logging::Logger::trace(const T& msg)
{
    log(logging::trace, msg);
}

template <typename T>
inline void logging::Logger::debug(const T& msg)
{
    log(logging::debug, msg);
}

template <typename T>
inline void logging::Logger::info(const T& msg)
{
    log(logging::info, msg);
}

template <typename T>
inline void logging::Logger::warn(const T& msg)
{
    log(logging::warn, msg);
}

template <typename T>
inline void logging::Logger::error(const T& msg)
{
    log(logging::err, msg);
}

template <typename T>
inline void logging::Logger::fatal(const T& msg)
{
    log(logging::fatal, msg);
}

#endif // RWTE_LOGGING_H
