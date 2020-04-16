#pragma once
#include <rebind/Ref.h>

namespace rebind {

/******************************************************************************/

enum class Loc : rebind_tag {Trivial, Relocatable, Stack, Heap};

/******************************************************************************/

struct Copyable;

union Storage {
    void *pointer;
    char data[24];
};

template <class T>
static constexpr Loc loc_of = Loc::Heap;

struct Value;

template <class T>
static constexpr bool is_manageable = is_usable<T> && !std::is_same_v<T, Value>;

// parts::alloc_to<T>(&storage, static_cast<Args &&>(args)...);
        // if (std::is_trivially_copyable_v<T>) loc = Loc::Trivial;
        // else if (is_trivially_relocatable_v<T>) loc = Loc::Relocatable;
        // else loc = Loc::Stack;

/******************************************************************************/

struct Value {
    TagIndex idx;
    Storage storage;

    Value() noexcept = default;

    Value(std::nullptr_t) noexcept : Value() {}

    template <class T, class ...Args, std::enable_if_t<is_manageable<T>, int> = 0>
    Value(Type<T> t, Args &&...args);

    template <class T, std::enable_if_t<is_manageable<unqualified<T>>, int> = 0>
    Value(T &&t) : Value{Type<unqualified<T>>(), static_cast<T &&>(t)} {}

    Value(Value &&v) noexcept;
    Value(Value const &v);// = delete;

    Value &operator=(Value &&v) noexcept;
    Value &operator=(Value const &v);// = delete;

    ~Value() {destruct();}

    /**************************************************************************/

    bool destruct() noexcept;

    void reset() noexcept {if (destruct()) release();}

    void release() noexcept {idx.reset();}

    Loc location() const noexcept {return static_cast<Loc>(idx.tag());}

    Index index() const noexcept {return Index(idx);}

    std::string_view name() const noexcept {return index().name();}

    constexpr bool has_value() const noexcept {return idx.has_value();}

    explicit constexpr operator bool() const noexcept {return has_value();}

    /**************************************************************************/

    template <class T, class ...Args>
    T & emplace(Type<T>, Args &&...args);

    template <class T, std::enable_if_t<!is_type<T>, int> = 0>
    unqualified<T> & emplace(T &&t) {
        return emplace(Type<unqualified<T>>(), static_cast<T &&>(t));
    }

    /**************************************************************************/

    std::optional<Copyable> clone() const;

    template <class T>
    T const *target(Type<T> t={}) const {
        if (!index().equals<T>()) return nullptr;
        else if (location() == Loc::Heap) return static_cast<T const *>(storage.pointer);
        else return aligned_pointer<T>(&storage.data);
    }

    template <class T>
    T *target(Type<T> t={}) {return const_cast<T *>(std::as_const(*this).target(t));}


    // template <class T, class ...Args>
    // static Value from(Args &&...args) {
    //     static_assert(std::is_constructible_v<T, Args &&...>);
    //     return Value(new T{static_cast<Args &&>(args)...});
    // }

    template <class T>
    std::optional<T> load() const {
        std::optional<T> out;
        if (auto t = target<T>()) {
            DUMP("load exact match");
            out.emplace(*t);
        } else {
            DUMP("trying indirect load");
            Storage s;
            Target o{&s, Index::of<T>(), sizeof(s), Target::Stack};
            auto c = Load::call(index(), o, storage.pointer, Const);
            if (c == Load::heap) {
                auto &t = *reinterpret_cast<T *>(s.pointer);
                out.emplace(std::move(t));
                Destruct::impl<T>::put(t, Destruct::heap);
            } else if (c == Load::stack) {
                auto &t = *aligned_pointer<T>(&s.data);
                out.emplace(std::move(t));
                Destruct::impl<T>::put(t, Destruct::stack);
            }
        }
        return out;
    }

    /**************************************************************************/

    template <class T=Value, class ...Ts>
    auto call(Caller c, Ts &&...ts) const {
        DUMP(type_name<T>());
        return parts::call<T>(index(), Const, storage.pointer, c, static_cast<Ts &&>(ts)...);
    }

    // template <class ...Ts>
    // Value call_value(Caller c, Ts &&...ts) const {
    //     return parts::call_value(index(), Const, storage.pointer, c, static_cast<Ts &&>(ts)...);
    // }
    // template <class T>
    // Maybe<T> request(Scope &s, Type<T> t={}) const &;
    //  {
        // if constexpr(std::is_convertible_v<Value const &, T>) return some<T>(*this);
        // else return parts::request(index(), address(), s, t, Const);
    // }

    // template <class T>
    // Maybe<T> request(Scope &s, Type<T> t={}) &;
    // {
        // if constexpr(std::is_convertible_v<Value &, T>) return some<T>(*this);
        // else return parts::request(index(), address(), s, t, Lvalue);
    // }

    // template <class T>
    // Maybe<T> request(Scope &s, Type<T> t={}) &&;
    // {
        // if constexpr(std::is_convertible_v<Value &&, T>) return some<T>(std::move(*this));
        // else return parts::request(index(), address(), s, t, Rvalue);
    // }

    /**************************************************************************/

    // template <class T>
    // Maybe<T> request(Type<T> t={}) const & {Scope s; return request(s, t);}

    // template <class T>
    // Maybe<T> request(Type<T> t={}) & {Scope s; return request(s, t);}

    // template <class T>
    // Maybe<T> request(Type<T> t={}) && {Scope s; return std::move(*this).request(s, t);}

    /**************************************************************************/

    // template <class T>
    // T cast(Scope &s, Type<T> t={}) const & {
    //     if (auto p = from_ref(s, t)) return static_cast<T &&>(*p);
    //     throw std::move(s.set_error("invalid cast (rebind::Value const &)"));
    // }

    // template <class T>
    // T cast(Scope &s, Type<T> t={}) & {
    //     if (auto p = from_ref(s, t)) return static_cast<T &&>(*p);
    //     throw std::move(s.set_error("invalid cast (rebind::Value &)"));
    // }

    // template <class T>
    // T cast(Scope &s, Type<T> t={}) && {
    //     if (auto p = from_ref(s, t)) return static_cast<T &&>(*p);
    //     throw std::move(s.set_error("invalid cast (rebind::Value &&)"));
    // }

    // bool assign_if(Ref const &p) {return stat::assign_if::ok == parts::assign_if(index(), address(), p);}

    /**************************************************************************/

    // template <class T>
    // T cast(Type<T> t={}) const & {Scope s; return cast(s, t);}

    // template <class T>
    // T cast(Type<T> t={}) & {Scope s; return cast(s, t);}

    // template <class T>
    // T cast(Type<T> t={}) && {Scope s; return std::move(*this).cast(s, t);}

    /**************************************************************************/

    bool load_to(Target &v) const;// const & {return stat::request::ok == parts::request_to(v, index(), address(), Const);}
    // bool request_to(Output &v) & {return stat::request::ok == parts::request_to(v, index(), address(), Lvalue);}
    // bool request_to(Output &v) && {return stat::request::ok == parts::request_to(v, index(), address(), Rvalue);}

    /**************************************************************************/

    // template <class ...Args>
    // Value operator()(Args &&...args) const;

    // bool call_to(Value &, ArgView) const;
};

/******************************************************************************/

template <class T, class ...Args, std::enable_if_t<is_manageable<T>, int>>
Value::Value(Type<T> t, Args&& ...args) {
    if constexpr(loc_of<T> == Loc::Heap) {
        storage.pointer = parts::alloc<T>(static_cast<Args &&>(args)...);
    } else {
        parts::alloc_to<T>(&storage.data, static_cast<Args &&>(args)...);
    }
    idx = TagIndex(Index::of<T>(), static_cast<rebind_tag>(loc_of<T>));
    // DUMP("construct! ", idx, " ", index().name());
}

/******************************************************************************/

template <class T, class ...Args>
T & Value::emplace(Type<T>, Args &&...args) {
    assert_usable<T>();
    reset();
    T *out;
    if constexpr(loc_of<T> == Loc::Heap) {
        storage.pointer = out = parts::alloc<T>(static_cast<Args &&>(args)...);
    } else {
        out = parts::alloc_to<T>(&storage.data, static_cast<Args &&>(args)...);
    }
    idx = TagIndex(Index::of<T>(), static_cast<rebind_tag>(loc_of<T>));
    // DUMP("emplace! ", idx, " ", index().name(), " ", Index::of<T>().name());
    return *out;
}

/******************************************************************************/

inline Value::Value(Value&& v) noexcept : idx(v.idx), storage(v.storage) {
    if (location() == Loc::Stack) {
        Relocate::call(index(), &storage.data, &v.storage.data);
    }
    v.release();
}

inline Value& Value::operator=(Value&& v) noexcept {
    reset();
    idx = v.idx;
    if (location() == Loc::Stack) {
        Relocate::call(index(), &storage.data, &v.storage.data);
    } else {
        storage = v.storage;
    }
    v.release();
    return *this;
}

inline bool Value::destruct() noexcept {
    if (!has_value()) return false;
    switch (location()) {
        case Loc::Trivial: break;
        case Loc::Heap: {Destruct::call(index(), storage.pointer, Destruct::heap); break;}
        default: {Destruct::call(index(), &storage, Destruct::stack); break;}
    }
    return true;
}

/******************************************************************************/

template <>
struct CallReturn<Value> {
    template <class ...Ts>
    static Value call(Index i, Tag qualifier, void *self, Caller &c, Arg<Ts &&> ...ts) {
        Value out;
        DUMP("calling something...");
        ArgStack<sizeof...(Ts)> args(c, ts.ref()...); // Ts && now safely scheduled for destruction
        Target target{&out.storage, Index(), sizeof(Storage), Target::Movable};
        auto const stat = Call::call(i, &target, self, qualifier, reinterpret_cast<ArgView &>(args));
        DUMP("called the output! ", stat);

        if (stat == Call::none) {
            // fine
        } else if (stat == Call::in_place) {
            out.idx = TagIndex(target.idx, static_cast<rebind_tag>(Loc::Stack));
        } else if (stat == Call::heap) {
            out.idx = TagIndex(target.idx, static_cast<rebind_tag>(Loc::Heap));
        } else {
            // function is noexcept until here, now it is permitted to throw (I think)
            handle_call_errors(stat, target);
        }
        return out;
    }
};

// struct Copyable : Value {
//     using Value::Value;
//     Copyable(Copyable &&) noexcept = default;
//     Copyable &operator=(Copyable &&v) noexcept = default;

//     Copyable(Copyable const &v) {*this = v;}

//     Copyable &operator=(Copyable const &v) {
//         switch (location()) {
//             case Loc::Trivial: {storage = v.storage; break;}
//             case Loc::Relocatable: {auto t = &storage.data; break;}
//             case Loc::Stack: {auto t = &storage.data; break;}
//             case Loc::Heap: {auto t = storage.pointer; break;}
//         }
//         idx = v.idx;
//         return *this;
//     }

//     Copyable clone() const {return *this;}
// };

/******************************************************************************/

// struct {
    // Value(Value const &v) {
    //     if (stat::copy::ok != parts::copy(*this, v.index(), v.address())) throw std::runtime_error("no copy");
    // }

    // Value &operator=(Value const &v) {
    //     if (stat::copy::ok != parts::copy(*this, v.index(), v.address())) throw std::runtime_error("no copy");
    //     return *this;
    // }
// }

/******************************************************************************/

}