#include <ctime>
#include <unordered_map>

#include "rwte/logging.h"

// global logger map
static std::unordered_map<std::string, std::shared_ptr<logging::Logger>> g_loggers;

static const char *level_names[] {
    "TRACE",
    "DEBUG",
    " INFO",
    " WARN",
    "ERROR",
    "FATAL",
    "OTHER"
};

std::shared_ptr<logging::Logger> logging::get(const std::string& name)
{
    auto found = g_loggers.find(name);
    if (found != g_loggers.end())
        return found->second;
    else
    {
        auto logger = std::make_shared<logging::Logger>(name);
        g_loggers[name] = logger;
        return logger;
    }
}

void logging::details::log_message(const logging::details::Message& msg)
{
    time_t ts = std::chrono::system_clock::to_time_t(msg.ts);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        msg.ts.time_since_epoch()).count();

    std::tm ltm = {0};
    localtime_r(&ts, &ltm);

    char timestr[sizeof "YYYY-MM-DDTHH:MM:SS.NNN"];
    sprintf(timestr, "%04d-%02d-%02dT%02d:%02d:%02d.%03ld",
        ltm.tm_year+1900, ltm.tm_mon+1, ltm.tm_mday,
        ltm.tm_hour, ltm.tm_min, ltm.tm_sec,
        ms % 1000);

    int level = msg.level;
    if (level < logging::trace || logging::off < level)
        level = logging::off; // OTHER
    const char * levelstr = level_names[level];

    printf("%s %s %s - %s\n", timestr, levelstr, msg.logname->c_str(), msg.msg.c_str());
}
