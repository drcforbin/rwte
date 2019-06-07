#ifndef RWTE_BUS_H
#define RWTE_BUS_H

#include <algorithm>
#include <functional>
#include <vector>
#include <tuple>

namespace detail {

template <typename T>
class ETag { using type = T; };

template<class EvtT>
class BusFuncs {
    using Fn = std::function<void(const EvtT&)>;
    using IdxFn = std::tuple<int, Fn>;

public:
    int reg(Fn fn) {
        int idx = nextidx++;
        calls.emplace_back(idx, fn);
        return idx;
    }

    void unreg(int key) {
        calls.erase(
            std::find_if(calls.begin(), calls.end(),
                [key](const auto& t){ return std::get<0>(t) == key; }));
    }

    template<class L, void (L::*method)(const EvtT&)>
    int reg(L *obj) {
        return reg(std::bind(method, obj, std::placeholders::_1));
    }

    template<class L, void (L::*method)(const EvtT&)>
    void unreg(L *obj) {
        unreg(std::bind(method, obj, std::placeholders::_1));
    }

    void publish(const EvtT& evt) {
        for (auto& c : calls)
            std::get<1>(c)(evt);
    }

private:
    int nextidx = 0;
    std::vector<IdxFn> calls;
};

// forward, so we can use it as a base
template<int S, typename... EvtTs>
class BusBase;

template<int S, typename EvtT, typename... EvtTs>
class BusBase<S, EvtT, EvtTs...> : public BusBase<S, EvtTs...>{
    using Base = BusBase<S, EvtTs...>;

protected:
    using Base::get;

    BusFuncs<EvtT>& get(ETag<EvtT>) { return funcs; }

private:
    BusFuncs<EvtT> funcs;
};

// base def, for no handler
template<int S>
class BusBase<S> {
protected:
    void get();
};

} // namespace detail

template <typename... EvtTs>
class Bus : public detail::BusBase<sizeof...(EvtTs), EvtTs...>
{
    using Base = detail::BusBase<sizeof...(EvtTs), EvtTs...>;

public:
    template<typename EvtT, class L, void (L::*method)(const EvtT&)>
    int reg(L *obj) {
        return Base::get(detail::ETag<EvtT>{}).template reg<L, method>(obj);
    }

    /*
    template<typename EvtT>
    void reg(Listener<EvtT> *obj) {
        Base::reg(obj);
    }
    */

    template<typename EvtT>
    void unreg(int idx) {
        Base::get(detail::ETag<EvtT>{}).unreg(idx);
    }

    /*
    template<typename EvtT, class L, void (L::*method)(const EvtT&)>
    void unreg(L *obj) {
        Base::get(detail::ETag<EvtT>{}).template unreg<L, method>(obj);
    }
    */

    /*
    template<typename EvtT>
    void unreg(Listener<EvtT> *obj) {
        Base::unreg(obj);
    }
    */

    template<typename EvtT>
    void publish(const EvtT& evt) {
        Base::get(detail::ETag<EvtT>{}).publish(evt);
    }
};

#endif // RWTE_BUS_H
