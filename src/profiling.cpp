#include "rwte/logging.h"
#include "rwte/profiling.h"

#include <unordered_map>

// global profiler map
static std::unordered_map<std::string, std::shared_ptr<profiling::Profiler>> g_profilers;

std::shared_ptr<profiling::Profiler> profiling::get(const std::string& name)
{
    auto found = g_profilers.find(name);
    if (found != g_profilers.end())
        return found->second;
    else
    {
        auto profiler = std::make_shared<profiling::Profiler>(name);
        g_profilers[name] = profiler;
        return profiler;
    }
}

void profiling::dump_and_clear()
{
    auto logger = logging::get("prof");

    for (auto& p : g_profilers)
    {
        if (p.second->count() > 0)
        {
            auto us = std::chrono::duration<double, std::milli>(
                    p.second->total());
            logger->info("{}: {:0.3f}ms, {}x", p.first,
                    us.count() / (double) p.second->count(),
                    p.second->count());
            p.second->reset();
        }
    }
}
