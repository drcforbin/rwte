#include "fmt/time.h"
#include "rwte/logging.h"

#include <ctime>
#include <unordered_map>

// global logger map
static std::unordered_map<std::string, std::shared_ptr<logging::Logger>> g_loggers;

const char* const level_names[]{
        "TRACE",
        "DEBUG",
        " INFO",
        " WARN",
        "ERROR",
        "FATAL",
        "OTHER"};

std::shared_ptr<logging::Logger> logging::get(const std::string& name)
{
    auto found = g_loggers.find(name);
    if (found != g_loggers.end())
        return found->second;
    else {
        auto logger = std::make_shared<logging::Logger>(name);
        g_loggers[name] = logger;
        return logger;
    }
}

void logging::details::log_message(const logging::details::Message& msg)
{
    auto ts = std::chrono::system_clock::to_time_t(msg.ts);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            msg.ts.time_since_epoch())
                      .count();

    std::tm ltm = {0};
    localtime_r(&ts, &ltm);

    int level = msg.level;
    if (level < logging::trace || logging::off < level)
        level = logging::off; // OTHER
    const char* levelstr = level_names[level];

    fmt::print("{:%Y-%m-%dT%H:%M:%S}.{:03d} {} {} - {}\n",
            ltm, ms % 1000, levelstr, *msg.logname, msg.msg);
}
