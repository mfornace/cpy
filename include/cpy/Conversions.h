#pragma once
#include "Variable.h"

namespace cpy {

template <class T, class=void>
struct ImplicitConversions {
    using types = Pack<>;
};

template <class U, class T>
bool implicit_match(Variable &out, Type<U>, T &&t, Qualifier const q) {
    DUMP("implicit_match", typeid(U).name(), typeid(Type<T &&>).name(), q);
    if constexpr(std::is_convertible_v<T &&, U>)
        if (q == Qualifier::V) out = {Type<U>(), static_cast<T &&>(t)};
    if constexpr(std::is_convertible_v<T &&, U &>)
        if (q == Qualifier::L) out = {Type<U &>(), static_cast<T &&>(t)};
    if constexpr(std::is_convertible_v<T &&, U &&>)
        if (q == Qualifier::R) out = {Type<U &&>(), static_cast<T &&>(t)};
    if constexpr(std::is_convertible_v<T &&, U const &>)
        if (q == Qualifier::C) out = {Type<U const &>(), static_cast<T &&>(t)};
    DUMP("implicit_response result ", out.has_value(), typeid(Type<T &&>).name(), typeid(U).name(), q);
    return out.has_value();
}

template <class U, class T>
bool recurse_implicit(Variable &out, Type<U>, T &&t, std::type_index idx, Qualifier q);

template <class T>
bool implicit_response(Variable &out, T &&t, std::type_index idx, Qualifier q) {
    DUMP("implicit_response", typeid(Type<T &&>).name(), idx.name(), typeid(typename ImplicitConversions<std::decay_t<T>>::types).name(), q);
    return ImplicitConversions<std::decay_t<T>>::types::apply([&](auto ...ts) {
        static_assert((!decltype(is_same(+Type<T>(), +ts))::value && ...), "Implicit conversion creates a cycle");
        return ((std::type_index(ts) == idx && implicit_match(out, ts, static_cast<T &&>(t), q)) || ...)
            || (recurse_implicit(out, ts, static_cast<T &&>(t), idx, q) || ...);
    });
}

template <class U, class T>
bool recurse_implicit(Variable &out, Type<U>, T &&t, std::type_index idx, Qualifier q) {
    if constexpr(std::is_convertible_v<T &&, U &&>)
        return implicit_response(out, static_cast<U &&>(t), idx, q);
    else if constexpr(std::is_convertible_v<T &&, U &>)
        return implicit_response(out, static_cast<U &>(t), idx, q);
    else if constexpr(std::is_convertible_v<T &&, U const &>)
        return implicit_response(out, static_cast<U const &>(t), idx, q);
    else if constexpr(std::is_convertible_v<T &&, U>)
        return implicit_response(out, static_cast<U>(t), idx, q);
    return false;
}

/******************************************************************************/

template <class T, unsigned Q, class SFINAE=void>
struct QualifiedResponse;

template <class T, class SFINAE>
struct QualifiedResponse<T, 0, SFINAE> {
    bool operator()(Variable &out, T const &t, std::type_index idx) const {
        DUMP("default simplifyvalue ", typeid(T).name());
        return implicit_response(out, t, idx, Qualifier::V);
    }
};

template <class T>
struct QualifiedResponse<T, 0, std::void_t<decltype(response(std::declval<T const &>(), std::declval<std::type_index &&>()))>> {
    bool operator()(Variable &out, T const &t, std::type_index idx) const {
        DUMP("adl simplifyvalue", typeid(T).name());
        out = response(t, std::move(idx));
        return out || implicit_response(out, t, idx, Qualifier::V);
    }
};

/******************************************************************************/

template <class T, class SFINAE>
struct QualifiedResponse<T, 1, SFINAE> {
    bool operator()(Variable &out, T const &t, std::type_index idx) const {
        DUMP("no conversion for const reference ", typeid(T).name(), idx.name());
        return implicit_response(out, t, idx, c_qualifier());
    }
};

template <class T, class SFINAE>
struct QualifiedResponse<T, 2, SFINAE> {
    bool operator()(Variable &out, T &t, std::type_index idx) const {
        DUMP("no conversion for lvalue reference ", typeid(T).name(), idx.name());
        return implicit_response(out, t, idx, l_qualifier());
    }
};

template <class T, class SFINAE>
struct QualifiedResponse<T, 3, SFINAE> {
    bool operator()(Variable &out, T &&t, std::type_index idx) const {
        DUMP("no conversion for rvalue reference ", typeid(T).name(), idx.name());
        return implicit_response(out, std::move(t), idx, r_qualifier());
    }
};

/******************************************************************************/

template <class T>
struct QualifiedResponse<T, 1, std::void_t<decltype(response(std::declval<T const &>(), std::declval<std::type_index &&>(), c_qualifier()))>> {
    template <class Q>
    bool operator()(Variable &out, T const &t, std::type_index idx) const {
        DUMP("convert reference via ADL ", typeid(T).name());
        out = response(static_cast<decltype(t) &&>(t), std::move(idx), c_qualifier());
        return out || implicit_response(out, t, idx, c_qualifier());
    }
};

template <class T>
struct QualifiedResponse<T, 2, std::void_t<decltype(response(std::declval<T const &>(), std::declval<std::type_index &&>(), l_qualifier()))>> {
    template <class Q>
    bool operator()(Variable &out, T &t, std::type_index idx) const {
        DUMP("convert reference via ADL ", typeid(T).name());
        out = response(static_cast<decltype(t) &&>(t), std::move(idx), l_qualifier());
        return out || implicit_response(out, t, idx, l_qualifier());
    }
};

template <class T>
struct QualifiedResponse<T, 3, std::void_t<decltype(response(std::declval<T const &>(), std::declval<std::type_index &&>(), r_qualifier()))>> {
    template <class Q>
    bool operator()(Variable &out, T &&t, std::type_index idx) const {
        DUMP("convert reference via ADL ", typeid(T).name());
        out = response(static_cast<decltype(t) &&>(t), std::move(idx), r_qualifier());
        return out || implicit_response(out, t, idx, r_qualifier());
    }
};

/******************************************************************************/

template <class T, class>
struct Response {
    static_assert(std::is_same_v<no_qualifier<T>, T>);

    bool operator()(Variable &out, T const &t, std::type_index idx, v_qualifier={}) const {
        return QualifiedResponse<T, 0>()(out, t, idx);
    }

    bool operator()(Variable &out, T const &t, std::type_index idx, c_qualifier) const {
        return QualifiedResponse<T, 1>()(out, t, idx);
    }

    bool operator()(Variable &out, T &t, std::type_index idx, l_qualifier) const {
        return QualifiedResponse<T, 2>()(out, t, idx);
    }

    bool operator()(Variable &out, T &&t, std::type_index idx, r_qualifier) const {
        return QualifiedResponse<T, 3>()(out, std::move(t), idx);
    }
};

/******************************************************************************/

/// The 4 default behaviors for casting a reference to an expected type
template <class T, class>
struct Request {
    static_assert(std::is_same_v<no_qualifier<T>, T>);

    std::optional<T> operator()(Variable const &r, Dispatch &msg) const {
        return msg.error("mismatched class type", typeid(T));
    }
};

void lvalue_fails(Variable const &, Dispatch &, std::type_index);
void rvalue_fails(Variable const &, Dispatch &, std::type_index);

template <class T, class C>
struct Request<T &, C> {
    T * operator()(Variable const &v, Dispatch &msg) const {
        lvalue_fails(v, msg, typeid(T));
        return nullptr;
    }
};

template <class T, class C>
struct Request<T const &, C> {
    T const * operator()(Variable const &v, Dispatch &msg) const {
        DUMP("trying & -> const & ", typeid(T).name());
        if (auto p = v.request<T &>(msg)) return p;
        DUMP("trying temporary const & storage ", typeid(T).name());
        if (auto p = v.request<T>(msg)) return msg.store(std::move(*p));
        return msg.error("could not bind to const l_qualifier reference", typeid(T)), nullptr;
    }
};

template <class T, class C>
struct Request<T &&, C> {
    T * operator()(Variable const &v, Dispatch &msg) const {
        DUMP("trying temporary && storage ", typeid(T).name());
        if (auto p = v.request<T>(msg)) return msg.store(std::move(*p));
        rvalue_fails(v, msg, typeid(T));
        return nullptr;
    }
};


void request(int, int, int);

/// ADL version
template <class T>
struct Request<T, std::void_t<decltype(request(Type<T>(), std::declval<Variable const &>(), std::declval<Dispatch &>()))>> {

    std::optional<T> operator()(Variable const &r, Dispatch &msg) const {
        return static_cast<std::optional<T>>(request(Type<T>(), r, msg));
    }
};

/******************************************************************************/

}
