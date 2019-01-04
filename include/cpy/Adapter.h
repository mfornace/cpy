#pragma once
#include "Signature.h"
#include "Types.h"

namespace cpy {


/******************************************************************************/

/// Invoke a function and arguments, storing output in a Variable if it doesn't return void
template <class F, class ...Ts>
Variable variable_invoke(F const &f, Ts &&... ts) {
    using O = std::remove_cv_t<std::invoke_result_t<F, Ts...>>;
    DUMP("    -- making output ", typeid(Type<O>).name());
    if constexpr(std::is_same_v<void, O>) {
        std::invoke(f, static_cast<Ts &&>(ts)...);
        return {};
    } else return Variable(Type<O>(), std::invoke(f, static_cast<Ts &&>(ts)...));
}

template <class F, class ...Ts>
Variable caller_invoke(std::true_type, F const &f, Caller &&c, Ts &&...ts) {
    DUMP("    - invoking with context");
    c.enter();
    return variable_invoke(f, std::move(c), static_cast<Ts &&>(ts)...);
}

template <class F, class ...Ts>
Variable caller_invoke(std::false_type, F const &f, Caller &&c, Ts &&...ts) {
    DUMP("    - invoking context guard");
    c.enter();
    DUMP("    - invoked context guard");
    return variable_invoke(f, static_cast<Ts &&>(ts)...);
}

/******************************************************************************/

inline Type<void> simplify_argument(Type<void>) {return {};}

/// Check type and remove cv qualifiers on arguments that are not lvalues
template <class T>
auto simplify_argument(Type<T>) {
    using U = std::remove_reference_t<T>;
    static_assert(!std::is_volatile_v<U> || !std::is_reference_v<T>, "volatile references are not supported");
    using V = std::conditional_t<std::is_lvalue_reference_v<T>, U &, std::remove_cv_t<U>>;
    using Out = std::conditional_t<std::is_rvalue_reference_v<T>, V &&, V>;
    static_assert(std::is_convertible_v<Out, T>, "simplified type should be compatible with original");
    return Type<Out>();
}

template <class ...Ts>
Pack<decltype(*simplify_argument(Type<Ts>()))...> simplify_signature(Pack<Ts...>) {return {};}

template <class F>
using SimpleSignature = decltype(simplify_signature(Signature<F>()));

template <int N, class R, class ...Ts, std::enable_if_t<N == 1, int> = 0>
Pack<Ts...> skip_head(Pack<R, Ts...>);

template <int N, class R, class C, class ...Ts, std::enable_if_t<N == 2, int> = 0>
Pack<Ts...> skip_head(Pack<R, C, Ts...>);

template <class C, class R>
std::false_type has_head(Pack<R>);

template <class C, class R, class T, class ...Ts>
std::is_convertible<T, C> has_head(Pack<R, T, Ts...>);

/******************************************************************************/

template <std::size_t N, class F, class SFINAE=void>
struct Adapter {
    F function;
    using Ctx = decltype(has_head<Caller>(SimpleSignature<F>()));
    using Sig = decltype(skip_head<1 + int(Ctx::value)>(SimpleSignature<F>()));

    template <class P>
    void call_each(P, Variable &out, Caller &&c, Dispatch &msg, Sequence &args) const {
        P::indexed([&](auto ...ts) {
            out = caller_invoke(Ctx(), function, std::move(c), cast_index(args, msg, simplify_argument_type(ts))...);
        });
    }

    template <std::size_t ...Is>
    Variable call(Sequence &args, Caller &&c, Dispatch &msg, std::index_sequence<Is...>) const {
        Variable out;
        ((args.size() == N - Is - 1 ? call_each(Sig::template slice<0, N - Is - 1>(), out, std::move(c), msg, args) : void()), ...);
        return out;
    }

    Variable operator()(Caller c, Sequence args) const {
        auto frame = c();
        Caller handle(frame);
        Dispatch msg(handle);
        if (args.size() == Sig::size)
            return Sig::indexed([&](auto ...ts) {
                return caller_invoke(Ctx(), function, std::move(handle), cast_index(args, msg, simplify_argument_type(ts))...);
            });
        else if (args.size() < Sig::size - N)
            throw WrongNumber(Sig::size - N, args.size());
        else if (args.size() > Sig::size)
            throw WrongNumber(Sig::size, args.size());
        return call(args, std::move(handle), msg, std::make_index_sequence<N>());
    }
};

/******************************************************************************/

template <class F, class SFINAE>
struct Adapter<0, F, SFINAE> {
    F function;
    using Ctx = decltype(has_head<Caller>(SimpleSignature<F>()));
    using Sig = decltype(skip_head<1 + int(Ctx::value)>(SimpleSignature<F>()));

    Variable operator()(Caller c, Sequence args) const {
        if (args.size() != Sig::size)
            throw WrongNumber(Sig::size, args.size());
        return Sig::indexed([&](auto ...ts) {
            auto frame = c();
            Caller handle(frame);
            Dispatch msg(handle);
            return caller_invoke(Ctx(), function, std::move(handle), cast_index(args, msg, ts)...);
        });
    }
};

/******************************************************************************/

template <class R, class C>
struct Adapter<0, R C::*, std::enable_if_t<std::is_member_object_pointer_v<R C::*>>> {
    R C::* function;

    Variable operator()(Caller c, Sequence args) const {
        if (args.size() != 1) throw WrongNumber(1, args.size());
        auto &s = args[0];
        auto frame = c();
        Caller handle(frame);
        Dispatch msg(handle);
        if (s.qualifier() == Qualifier::L) {
            C &self = std::move(s).downcast<C &>(msg);
            frame->enter();
            return {Type<R &>(), std::invoke(function, self)};
        }
        if (s.qualifier() == Qualifier::C) {
            C const &self = std::move(s).downcast<C const &>(msg);
            frame->enter();
            return {Type<R const &>(), std::invoke(function, self)};
        }
        C self = std::move(s).downcast<C>(msg);
        frame->enter();
        return {Type<std::remove_cv_t<R>>(), std::invoke(function, std::move(self))};
    }
};

/******************************************************************************/

}