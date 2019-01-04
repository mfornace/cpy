#pragma once
#include "Signature.h"
#include <vector>
#include <iostream>
#include <string_view>

#define DUMP(...) ::cpy::dump(__FILE__, __LINE__, __VA_ARGS__);

namespace cpy {

extern bool const Debug;

template <class From, class To> struct copy_qualifier_t {using type = To;};
template <class From, class To> struct copy_qualifier_t<From &, To> {using type = To &;};
template <class From, class To> struct copy_qualifier_t<From const &, To> {using type = To const &;};
template <class From, class To> struct copy_qualifier_t<From &&, To> {using type = To &&;};

template <class From, class To> using copy_qualifier = typename copy_qualifier_t<From, To>::type;


template <class ...Ts>
void dump(char const *s, int n, Ts const &...ts) {
    if (!Debug) return;
    std::cout << s << ":" << n << " ";
    int x[] = {(std::cout << ts << " ", 0)...};
    std::cout << std::endl;
}

/******************************************************************************/

/// To avoid template type deduction on a given parameter
template <class T>
struct SameType {using type=T;};

/******************************************************************************/

template <class T>
using SizeOf = std::integral_constant<std::size_t, sizeof(T)>;

/// Binary search of an iterable of std::pair
template <class V>
auto binary_search(V const &v, typename V::value_type::first_type t) {
    auto it = std::lower_bound(v.begin(), v.end(), t,
        [](auto const &x, auto const &t) {return x.first < t;});
    return (it != v.end() && it->first == t) ? it : v.end();
}

/******************************************************************************/

template <class T>
using Vector = std::vector<T>;

template <class ...Ts>
struct ZipType {using type = std::tuple<Ts...>;};

template <class T, class U>
struct ZipType<T, U> {using type = std::pair<T, U>;};

template <class ...Ts>
using Zip = Vector<typename ZipType<Ts...>::type>;

template <class T, class V, class F>
Vector<T> mapped(V const &v, F &&f) {
    Vector<T> out;
    out.reserve(std::size(v));
    for (auto &&x : v) out.emplace_back(f(x));
    return out;
}

/******************************************************************************/

/// Interface: return a new frame given a shared_ptr of *this
struct Frame {
    virtual std::shared_ptr<Frame> operator()(std::shared_ptr<Frame> &&) = 0;
    virtual void enter() {};
    virtual ~Frame() {};
};

/******************************************************************************/

class Caller {
    std::weak_ptr<Frame> model;
public:
    Caller() = default;

    explicit operator bool() const {return !model.expired();}

    Caller(std::shared_ptr<Frame> const &f): model(f) {}

    void enter() {if (auto p = model.lock()) p->enter();}

    std::shared_ptr<Frame> operator()() const {
        if (auto p = model.lock()) return p.get()->operator()(std::move(p));
        return {};
    }

    template <class T>
    T * target() {
        if (auto p = model.lock()) return dynamic_cast<T *>(p.get());
        return nullptr;
    }
};

/******************************************************************************/

enum class Qualifier : unsigned char {V, C, L, R};

static std::string_view QualifierNames[4] = {"value", "const", "lvalue", "rvalue"};

inline std::ostream & operator<<(std::ostream &os, Qualifier q) {
    return os << QualifierNames[static_cast<unsigned char>(q)];
}

struct cvalue {constexpr operator Qualifier() const {return Qualifier::C;}};
struct lvalue {constexpr operator Qualifier() const {return Qualifier::L;}};
struct rvalue {constexpr operator Qualifier() const {return Qualifier::R;}};

template <class T, class Ref> struct Qualified;
template <class T> struct Qualified<T, cvalue> {using type = T const &;};
template <class T> struct Qualified<T, lvalue> {using type = T &;};
template <class T> struct Qualified<T, rvalue> {using type = T &&;};

template <class Ref, class T> using qualified = typename Qualified<Ref, T>::type;

template <class T>
static constexpr Qualifier qualifier_of = (!std::is_reference_v<T>) ? Qualifier::V
    : (std::is_rvalue_reference_v<T> ? Qualifier::R :
        (std::is_const_v<std::remove_reference_t<T>> ? Qualifier::C : Qualifier::L));

/******************************************************************************/

}