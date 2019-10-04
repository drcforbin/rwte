#ifndef PDATA_TEST_HELPERS_H
#define PDATA_TEST_HELPERS_H

#include "fmt/format.h"

#include <random>

struct MockHashable
{
    uint32_t hash = 0;
    int val = 0;
};

bool operator==(const MockHashable& lhs, const MockHashable& rhs)
{
    return lhs.val == rhs.val;
}

struct MockHashableHash
{
    std::size_t operator()(MockHashable const& m) const noexcept
    {
        return m.hash;
    }
};

template <>
struct fmt::formatter<MockHashable>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const MockHashable& m, FormatContext& ctx)
    {
        return format_to(ctx.out(), "h={}, v={}", m.hash, m.val);
    }
};

template <class IntType>
auto randomPairs(int count)
{
    // constant seed for random
    std::mt19937 gen(3289417ull);
    std::uniform_int_distribution<IntType> dis;

    std::vector<std::pair<IntType, IntType>> pairs(count);
    for (auto i = 0; i < count; i++) {
        pairs[i] = {dis(gen), dis(gen)};
    }

    return pairs;
}

template <class IntType>
auto randomDupPairs(std::vector<std::pair<IntType, IntType>> pairs)
{
    // constant seed for random
    std::mt19937 gen(3289999ull);
    std::uniform_int_distribution<IntType> dis;

    std::vector<std::pair<IntType, IntType>> newPairs(pairs.size());
    for (std::size_t i = 0; i < pairs.size(); i++) {
        newPairs[i] = {pairs[i].first, dis(gen)};
    }
    return newPairs;
}

template <class IntType>
std::shared_ptr<pdata::persistent_map<IntType, IntType>> fill(
        std::shared_ptr<pdata::persistent_map<IntType, IntType>> m,
        const std::vector<std::pair<IntType, IntType>>& pairs)
{
    for (auto pair : pairs) {
        m = m->assoc(pair.first, pair.second);
    }
    return m;
}

template <class IntType>
std::shared_ptr<pdata::persistent_map<IntType, IntType>> fillTransient(
        std::shared_ptr<pdata::persistent_map<IntType, IntType>> m,
        const std::vector<std::pair<IntType, IntType>>& pairs)
{
    auto t = m->transient();
    for (auto pair : pairs) {
        t = t->assoc(pair.first, pair.second);
    }
    return t->persistent();
}

template <class IntType>
bool check(std::shared_ptr<pdata::persistent_map<IntType, IntType>> m,
        const std::vector<std::pair<IntType, IntType>>& pairs)
{
    for (auto pair : pairs) {
        auto actual = m->find(pair.first);
        if (pair.second != actual) {
            return false;
        }
    }
    return true;
}

#endif // PDATA_TEST_HELPERS_H
