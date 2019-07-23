#ifndef RWTE_PROFILING_H
#define RWTE_PROFILING_H

#include <chrono>
#include <memory>
#include <string>

namespace profiling {
class Profiler
{
public:
    Profiler(std::string name) :
        m_name(std::move(name)),
        m_count(0),
        m_total(std::chrono::high_resolution_clock::duration())
    {}

    virtual ~Profiler();
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    const std::string& name() const { return m_name; }

    void begin() { m_start = std::chrono::high_resolution_clock::now(); }
    void end()
    {
        m_total += std::chrono::high_resolution_clock::now() - m_start;
        m_count++;
    }

    int count() const { return m_count; }
    const std::chrono::high_resolution_clock::duration& total() const
    {
        return m_total;
    }

    void reset()
    {
        m_count = 0;
        m_total = std::chrono::high_resolution_clock::duration();
    }

private:
    const std::string m_name;
    int m_count;
    std::chrono::high_resolution_clock::time_point m_start;
    std::chrono::high_resolution_clock::duration m_total;
};

// get/create a profiler
std::shared_ptr<Profiler> get(const std::string& name);
// print values
void dump_and_clear();

} // namespace profiling

// impl bits:

inline profiling::Profiler::~Profiler() = default;

#define PROF_DECL(name) \
    auto prof_##name = profiling::get(#name)

#define PROF_BEGIN(name) \
    prof_##name->begin()

#define PROF_END(name) \
    prof_##name->end()

#endif // RWTE_PROFILING_H
