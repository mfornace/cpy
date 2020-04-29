#pragma once
#include "Core.h"
#include <tuple>

namespace ara {

/******************************************************************************/

/// Dumpable for CompileSequence concept -- a sequence of compile time length
template <class T>
struct DumpCompiledSequence {
    using Array = std::array<Value, std::tuple_size_v<T>>;

    // template <std::size_t ...Is>
    // static Sequence sequence(T const &t, std::index_sequence<Is...>) {
    //     Sequence o;
    //     o.reserve(sizeof...(Is));
    //     (o.emplace_back(std::get<Is>(t)), ...);
    //     return o;
    // }

    template <std::size_t ...Is>
    static Sequence sequence(T &&t, std::index_sequence<Is...>) {
        Sequence o;
        o.reserve(sizeof...(Is));
        (o.emplace_back(std::get<Is>(std::move(t))), ...);
        return o;
    }

    // template <std::size_t ...Is>
    // static Array array(T const &t, std::index_sequence<Is...>) {return {std::get<Is>(t)...};}

    template <std::size_t ...Is>
    static Array array(T &&t, std::index_sequence<Is...>) {return {std::get<Is>(std::move(t))...};}

    bool operator()(Target &v, T const &t) const {
        auto idx = std::make_index_sequence<std::tuple_size_v<T>>();
        // if (v.accepts<Sequence>()) return v.set_if(sequence(t, idx));
        // if (v.accepts<Array>()) return v.set_if(array(t, idx));
        return false;
    }

    bool operator()(Target &v, T &&t) const {
        auto idx = std::make_index_sequence<std::tuple_size_v<T>>();
        if (v.accepts<Sequence>()) return v.set_if(sequence(std::move(t), idx));
        if (v.accepts<Array>()) return v.set_if(array(std::move(t), idx));
        return false;
    }
};


template <class V>
struct LoadCompiledSequence {
    using Array = std::array<Value, std::tuple_size_v<V>>;

    template <class ...Ts>
    static void combine(std::optional<V> &out, Ts &&...ts) {
        DUMP("trying LoadCompiledSequence combine", bool(ts)...);
        if ((bool(ts) && ...)) out = V{*static_cast<Ts &&>(ts)...};
    }

    template <class T, std::size_t ...Is>
    static void request_each(std::optional<V> &out, T &&t, std::index_sequence<Is...>) {
        DUMP("trying LoadCompiledSequence load");
        combine(out, std::move(t[Is]).load(Type<std::tuple_element_t<Is, V>>())...);
    }

    template <class T>
    static void load(std::optional<V> &out, T &&t) {
        DUMP("trying LoadCompiledSequence load");
        if (std::size(t) != std::tuple_size_v<V>) {
            // s.error("wrong sequence length", Index::of<V>(), std::tuple_size_v<V>, std::size(t));
        } else {
            // s.indices.emplace_back(0);
            request_each(out, std::move(t), std::make_index_sequence<std::tuple_size_v<V>>());
            // s.indices.pop_back();
        }
    }

    std::optional<V> operator()(Ref &r) const {
        std::optional<V> out;
        DUMP("trying LoadCompiledSequence", r.name());
        if constexpr(!std::is_same_v<V, Array>) {
            if (auto p = r.load<std::array<Value, std::tuple_size_v<V>>>()) {
                DUMP("trying array CompiledSequenceRequest2", r.name());
                load(out, std::move(*p));
            }
            return out;
        }
        if (auto p = r.load<Sequence>()) {
            DUMP("trying CompiledSequenceRequest2", r.name());
            load(out, std::move(*p));
        } else {
            DUMP("trying CompiledSequenceRequest3", r.name());
            // s.error("expected sequence to make compiled sequence", Index::of<V>());
        }
        return out;
    }
};

/******************************************************************************/

// Coverage of std::pair, std::array, and std::tuple. *Not* C arrays.
template <class T>
struct Loadable<T, std::enable_if_t<std::tuple_size<T>::value >= 0>> : LoadCompiledSequence<T> {};

template <class T>
struct Dumpable<T, std::enable_if_t<std::tuple_size<T>::value >= 0>> : DumpCompiledSequence<T> {};

/******************************************************************************/

}