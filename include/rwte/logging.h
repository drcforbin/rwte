#ifndef RWTE_LOGGING_H
#define RWTE_LOGGING_H

#include <memory>
#include <string>
#include <chrono>

#include "fmt/format.h"

namespace logging
{

using level_enum = enum
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
        Logger(const std::string& name) : m_name(name), m_level(logging::trace) { }

        virtual ~Logger();
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        const std::string& name() const { return m_name; }

        level_enum level() { return m_level; }
        void level(level_enum val) { m_level = val; }

        template <typename... Args> void log(level_enum lvl, const char* fmt, const Args&... args);
        template <typename... Args> void log(level_enum lvl, const char* msg);
        template <typename Arg1, typename... Args> void trace(const char* fmt, const Arg1&, const Args&... args);
        template <typename Arg1, typename... Args> void debug(const char* fmt, const Arg1&, const Args&... args);
        template <typename Arg1, typename... Args> void info(const char* fmt, const Arg1&, const Args&... args);
        template <typename Arg1, typename... Args> void warn(const char* fmt, const Arg1&, const Args&... args);
        template <typename Arg1, typename... Args> void error(const char* fmt, const Arg1&, const Args&... args);
        template <typename Arg1, typename... Args> void fatal(const char* fmt, const Arg1&, const Args&... args);

        template <typename T> void log(level_enum lvl, const T&);
        template <typename T> void trace(const T&);
        template <typename T> void debug(const T&);
        template <typename T> void info(const T&);
        template <typename T> void warn(const T&);
        template <typename T> void error(const T&);
        template <typename T> void fatal(const T&);
    private:
        const std::string m_name;
        level_enum m_level;
};

// get/create a logger
std::shared_ptr<Logger> get(const std::string& name);

namespace details
{
struct Message
{
    Message() = default;
    Message(const std::string *logname, logging::level_enum lvl) :
        logname(logname), level(lvl),
        ts(std::chrono::system_clock::now())
    { }

    Message(const Message& other)  = delete;
    Message& operator=(Message&& other) = delete;
    Message(Message&& other) = delete;

    const std::string *logname;
    level_enum level;
    std::chrono::system_clock::time_point ts;

    fmt::MemoryWriter msg;
};

void log_message(const Message& msg);

} // namespace details
} // namespace log

// impl bits:

inline logging::Logger::~Logger() = default;

template <typename... Args>
inline void logging::Logger::log(logging::level_enum lvl, const char* fmt, const Args&... args)
{
    if (lvl < m_level)
        return;

    details::Message message(&m_name, lvl);
    message.msg.write(fmt, args...);
    details::log_message(message);

    if (lvl == logging::fatal)
        exit(1);
}

template <typename... Args>
inline void logging::Logger::log(logging::level_enum lvl, const char* msg)
{
    if (lvl < m_level)
        return;

    details::Message message(&m_name, lvl);
    message.msg << msg;
    details::log_message(message);

    if (lvl == logging::fatal)
        exit(1);
}

template<typename T>
inline void logging::Logger::log(logging::level_enum lvl, const T& msg)
{
    if (lvl < m_level)
        return;

    details::Message message(&m_name, lvl);
    message.msg << msg;
    details::log_message(message);

    if (lvl == logging::fatal)
        exit(1);
}


template <typename Arg1, typename... Args>
inline void logging::Logger::trace(const char* fmt, const Arg1 &arg1, const Args&... args)
{
    log(logging::trace, fmt, arg1, args...);
}

template <typename Arg1, typename... Args>
inline void logging::Logger::debug(const char* fmt, const Arg1 &arg1, const Args&... args)
{
    log(logging::debug, fmt, arg1, args...);
}

template <typename Arg1, typename... Args>
inline void logging::Logger::info(const char* fmt, const Arg1 &arg1, const Args&... args)
{
    log(logging::info, fmt, arg1, args...);
}

template <typename Arg1, typename... Args>
inline void logging::Logger::warn(const char* fmt, const Arg1 &arg1, const Args&... args)
{
    log(logging::warn, fmt, arg1, args...);
}

template <typename Arg1, typename... Args>
inline void logging::Logger::error(const char* fmt, const Arg1 &arg1, const Args&... args)
{
    log(logging::err, fmt, arg1, args...);
}

template <typename Arg1, typename... Args>
inline void logging::Logger::fatal(const char* fmt, const Arg1 &arg1, const Args&... args)
{
    log(logging::fatal, fmt, arg1, args...);
}

template<typename T>
inline void logging::Logger::trace(const T& msg)
{
    log(logging::trace, msg);
}

template<typename T>
inline void logging::Logger::debug(const T& msg)
{
    log(logging::debug, msg);
}


template<typename T>
inline void logging::Logger::info(const T& msg)
{
    log(logging::info, msg);
}


template<typename T>
inline void logging::Logger::warn(const T& msg)
{
    log(logging::warn, msg);
}

template<typename T>
inline void logging::Logger::error(const T& msg)
{
    log(logging::err, msg);
}

template<typename T>
inline void logging::Logger::fatal(const T& msg)
{
    log(logging::fatal, msg);
}

#endif // RWTE_LOGGING_H
