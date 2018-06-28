#pragma once
#include "Approx.h"
#include "Value.h"
#include "Signature.h"
#include "Glue.h"

#include <functional>
#include <atomic>
#include <chrono>

namespace cpy {

/******************************************************************************/

using Event = std::uint_fast32_t;

static constexpr Event Failure   = 0;
static constexpr Event Success   = 1;
static constexpr Event Exception = 2;
static constexpr Event Timing    = 3;
static constexpr Event Skipped   = 4;

/******************************************************************************/

struct HandlerError : std::exception {
    std::string_view message;
    explicit HandlerError(std::string_view const &s) noexcept : message(s) {}
    char const * what() const noexcept override {return message.empty() ? "cpy::HandlerError" : message.data();}
};

using Scopes = std::vector<std::string>;

using Clock = std::chrono::high_resolution_clock;

using Callback = std::function<bool(Event, Scopes const &, Logs &&)>;

using Counter = std::atomic<std::size_t>;

/******************************************************************************/

struct Context {
    /// Vector of Callbacks for each registered Event
    std::vector<Callback> callbacks;
    /// Vector of strings making up the current Context scope
    Scopes scopes;
    /// Keypairs that have been logged prior to an event being called
    Logs logs;
    /// Start time of the current test case or section
    typename Clock::time_point start_time;
    /// Possibly null handle to a vector of atomic counters for each Event
    std::vector<Counter> *counters = nullptr;
    /// Metadata for use by handler. Test runner has responsibility for allocation/deallocation
    void *metadata = nullptr;

    Context() = default;

    /// Opens a Context and sets the start_time to the current time
    Context(Scopes s, std::vector<Callback> h, std::vector<Counter> *c=nullptr, void *m=nullptr);

    /// Opens a new section with a reset start_time
    template <class F, class ...Ts>
    auto section(std::string name, F &&functor, Ts &&...ts) const {
        Context ctx(scopes, callbacks, counters);
        ctx.scopes.push_back(std::move(name));
        return static_cast<F &&>(functor)(ctx, static_cast<Ts &&>(ts)...);
    }

    /**************************************************************************/

    Integer count(Event e, std::memory_order order=std::memory_order_relaxed) const {
        if (counters) return (*counters)[e].load(order);
        else return -1;
    }

    void info(std::string s) {logs.emplace_back(KeyPair{{}, std::move(s)});}

    void info(char const *s) {logs.emplace_back(KeyPair{{}, std::string_view(s)});}

    template <class T>
    void info(T &&t) {AddKeyPairs<std::decay_t<T>>()(logs, static_cast<T &&>(t));}

    template <class K, class V>
    void info(K &&k, V &&v) {
        logs.emplace_back(KeyPair{static_cast<K &&>(k), make_value(static_cast<V &&>(v))});
    }

    template <class ...Ts>
    Context & operator()(Ts &&...ts) {(info(static_cast<Ts &&>(ts)), ...); return *this;}

    /**************************************************************************/

    template <class ...Ts>
    void handle(Event e, Ts &&...ts) {
        if (e < callbacks.size() && callbacks[e]) {
            (*this)(static_cast<Ts &&>(ts)...);
            callbacks[e](e, scopes, std::move(logs));
        }
        if (counters && e < counters->size())
            (*counters)[e].fetch_add(1u, std::memory_order_relaxed);
        logs.clear();
    }

    template <class F, class ...Args>
    auto timed(std::size_t n, F &&f, Args &&...args) {
        auto const start = Clock::now();
        if constexpr(std::is_same_v<void, std::invoke_result_t<F &&, Args &&...>>) {
            std::invoke(static_cast<F &&>(f), static_cast<Args &&>(args)...);
            auto const elapsed = Clock::now() - start;
            handle(Timing, glue("value", elapsed));
            return elapsed;
        } else {
            auto result = std::invoke(static_cast<F &&>(f), static_cast<Args &&>(args)...);
            handle(Timing, glue("value", std::chrono::duration<double>(Clock::now() - start).count()));
            return result;
        }
    }

    template <class Bool=bool, class ...Ts>
    bool require(Bool const &ok, Ts &&...ts) {
        bool b = static_cast<bool>(unglue(ok));
        handle(b ? Success : Failure, static_cast<Ts &&>(ts)..., glue("value", ok));
        return b;
    }

    /******************************************************************************/

    template <class X, class Y, class ...Ts>
    bool equal(X const &x, Y const &y, Ts &&...ts) {
        return require(unglue(x) == unglue(y), comparison_glue(x, y, "=="), static_cast<Ts &&>(ts)...);
    }

    template <class X, class Y, class ...Ts>
    bool not_equal(X const &x, Y const &y, Ts &&...ts) {
        return require(unglue(x) != unglue(y), comparison_glue(x, y, "!="), static_cast<Ts &&>(ts)...);
    }

    template <class X, class Y, class ...Ts>
    bool less(X const &x, Y const &y, Ts &&...ts) {
        return require(unglue(x) < unglue(y), comparison_glue(x, y, "<"), static_cast<Ts &&>(ts)...);
    }

    template <class X, class Y, class ...Ts>
    bool greater(X const &x, Y const &y, Ts &&...ts) {
        return require(unglue(x) > unglue(y), comparison_glue(x, y, ">"), static_cast<Ts &&>(ts)...);
    }

    template <class X, class Y, class ...Ts>
    bool less_eq(X const &x, Y const &y, Ts &&...ts) {
        return require(unglue(x) <= unglue(y), comparison_glue(x, y, "<="), static_cast<Ts &&>(ts)...);
    }

    template <class X, class Y, class ...Ts>
    bool greater_eq(X const &x, Y const &y, Ts &&...ts) {
        return require(unglue(x) >= unglue(y), comparison_glue(x, y, ">="), static_cast<Ts &&>(ts)...);
    }

    template <class X, class Y, class T, class ...Ts>
    bool within(X const &x, Y const &y, T const &tol, Ts &&...ts) {
        ComparisonGlue<X const &, Y const &> expr{x, y, "~~"};
        if (x == y)
            return require(true, expr, static_cast<Ts &&>(ts)...);
        auto const a = x - y;
        auto const b = y - x;
        bool ok = (a < b) ? static_cast<bool>(b < tol) : static_cast<bool>(a < tol);
        return require(ok, expr, glue("tolerance", tol), glue("difference", b), static_cast<Ts &&>(ts)...);
    }

    template <class X, class Y, class ...Args>
    bool near(X const &x, Y const &y, Args &&...args) {
        bool ok = ApproxEquals<typename ApproxType<X, Y>::type>()(unglue(x), unglue(y));
        return require(ok, ComparisonGlue<X const &, Y const &>{x, y, "~~"}, static_cast<Args &&>(args)...);
    }

    template <class Exception, class F, class ...Args>
    bool throw_as(F &&f, Args &&...args) {
        try {
            std::invoke(static_cast<F &&>(f), static_cast<Args &&>(args)...);
            return require(false);
        } catch (Exception const &) {return require(true);}
    }

    template <class F, class ...Args>
    bool no_throw(F &&f, Args &&...args) {
        try {
            std::invoke(static_cast<F &&>(f), static_cast<Args &&>(args)...);
            return require(true);
        } catch (HandlerError const &e) {
            throw e;
        } catch (...) {return require(false);}
    }
};

/******************************************************************************/

}
