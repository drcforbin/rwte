#ifndef RWTE_BUS_H
#define RWTE_BUS_H

#include <algorithm>
#include <vector>

namespace detail {

template <typename T>
class ETag { using type = T; };

template<class EvtT>
class BusFuncs {
public:
    template<class L, void (L::*method)(const EvtT&)>
    void reg(L *obj) {
        calls.emplace_back(obj, &method_thunk<L, method>);
    }

    template<class L, void (L::*method)(const EvtT&)>
    void unreg(L *obj) {
        Call call(obj, &method_thunk<L, method>);
        calls.erase(std::remove(calls.begin(), calls.end(), call), calls.end());
    }

    void publish(const EvtT& evt) {
        for (auto& c : calls)
            c.f(c.p, evt);
    }

private:
    template<class L, void (L::*method)(const EvtT& evt)>
    static void method_thunk(void *wp, const EvtT& evt)
    {
        (static_cast<L*>(wp)->*method)(evt);
    }

    struct Call
    {
        Call(void *p, void(*f)(void *, const EvtT &)) :
            p(p), f(f)
        { }

        bool operator==(const Call &other) const noexcept {
            return p == other.p && f == other.f;
        }

        void *p;
        void(*f)(void *, const EvtT &);
    };

    std::vector<Call> calls;
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
class Bus : public detail::BusBase<sizeof...(EvtTs), EvtTs...>{
    using Base = detail::BusBase<sizeof...(EvtTs), EvtTs...>;

public:
    template<typename EvtT, class L, void (L::*method)(const EvtT&)>
    void reg(L *obj) {
        Base::get(detail::ETag<EvtT>{}).template reg<L, method>(obj);
    }

    /*
    template<typename EvtT>
    void reg(Listener<EvtT> *obj) {
        Base::reg(obj);
    }
    */

    template<typename EvtT, class L, void (L::*method)(const EvtT&)>
    void unreg(L *obj) {
        Base::get(detail::ETag<EvtT>{}).template unreg<L, method>(obj);
    }

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
