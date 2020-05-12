#pragma once
#include "Parts.h"
#include "Impl.h"

namespace ara {

/******************************************************************************************/

union Ref {
    ara_ref c;

    Ref() noexcept : c{nullptr, nullptr} {}
    Ref(std::nullptr_t) noexcept : Ref() {}

    template <class T, std::enable_if_t<std::is_reference_v<T>, int> = 0>
    explicit Ref(T &&t) noexcept
        : Ref(Index::of<unqualified<T>>(),
          std::is_const_v<std::remove_reference_t<T>> ? Tag::Const : Tag::Mutable,
          Pointer::from(const_cast<void*>(static_cast<void const*>(std::addressof(t))))) {}

    Ref(Tagged<Tag> i, Pointer p) noexcept : c{i.base, p.base} {}
    Ref(Index i, Tag t, Pointer p) noexcept : Ref{{i, t}, p} {}

    Ref(Ref &&r) noexcept : c{std::exchange(r.c.tag_index, nullptr), r.c.pointer} {}

    Ref &operator=(Ref &&r) noexcept {
        c.tag_index = std::exchange(r.c.tag_index, nullptr);
        c.pointer = r.c.pointer;
        return *this;
    }

    Ref(Ref const &) = delete;
    Ref &operator=(Ref const &) = delete;

    ~Ref() noexcept {destroy_if_managed();}

    /**********************************************************************************/

    Index index() const noexcept {return ara_get_index(c.tag_index);}
    Tag tag() const noexcept {return static_cast<Tag>(ara_get_tag(c.tag_index));}
    Pointer pointer() const noexcept {return bit_cast<Pointer>(c.pointer);}

    constexpr bool has_value() const noexcept {return c.tag_index;}
    explicit constexpr operator bool() const noexcept {return has_value();}

    std::string_view name() const noexcept {return index().name();}

    void destroy_if_managed() {
        if (!has_value()) return;
        switch (tag()) {
            case Tag::Stack: {Destruct::call(index(), pointer(), Destruct::Stack); c.tag_index = nullptr; return;}
            case Tag::Heap:  {Destruct::call(index(), pointer(), Destruct::Heap); c.tag_index = nullptr; return;}
            default: {return;}
        }
    }

    /**********************************************************************************/

    template <class T>
    bool binds_to(Qualifier q) const;

    template <class T, std::enable_if_t<std::is_reference_v<T>, int> = 0>
    std::remove_reference_t<T> *target() const {
        return binds_to<unqualified<T>>(qualifier_of<T>) ? pointer().address<std::remove_reference_t<T>>() : nullptr;
    }

    template <class T, int N=0, class ...Ts>
    T call(Caller c, Ts &&...ts) const {
        DUMP("Ref::call:", type_name<T>(), "(", sizeof...(Ts), ")");
        return parts::call<T, N>(index(), tag(), pointer().base, c, static_cast<Ts &&>(ts)...);
    }

    template <class T, int N=0, class ...Ts>
    maybe<T> get(Caller c, Ts &&...ts) const {
        if (!has_value()) return Maybe<T>::none();
        return parts::get<T, N>(index(), Tag::Const, pointer().base, c, static_cast<Ts &&>(ts)...);
    }

    /**************************************************************************************/

    template <class T, std::enable_if_t<!std::is_reference_v<T>, int> = 0>
    std::optional<T> load(Type<T> t={});

    template <class T, std::enable_if_t<std::is_reference_v<T>, int> = 0>
    std::remove_reference_t<T> * load(Type<T> t={}) const;

    template <class T, std::enable_if_t<!std::is_reference_v<T>, int> = 0>
    T cast(Type<T> t={});

    template <class T, std::enable_if_t<std::is_reference_v<T>, int> = 0>
    T cast(Type<T> t={}) const;

    /**************************************************************************************/

    Load::stat load_to(Target &t) noexcept;
};

/******************************************************************************************/

template <>
struct is_trivially_relocatable<Ref> : std::true_type {};

template <class T>
bool Ref::binds_to(Qualifier q) const {
    if (!index().equals<T>()) return false;
    if (q == Qualifier::C) return true;
    switch (tag()) {
        case Tag::Const: return false;
        case Tag::Mutable: return q == Qualifier::L;
        default: return q == Qualifier::R;
    }
}

inline Load::stat dump_or_load(Target &target, Index source, Pointer p, Tag t) noexcept {
    switch (Dump::call(source, target, p, t)) {
        case Dump::Mutable: {DUMP("OK"); return Load::Mutable;}
        case Dump::Const: {DUMP("OK"); return Load::Const;}
        case Dump::Heap: {DUMP("OK"); return Load::Heap;}
        case Dump::Stack: {DUMP("OK"); return Load::Stack;}
        case Dump::OutOfMemory: {return Load::OutOfMemory;}
        case Dump::Exception: {return Load::Exception;}
        case Dump::None: {
            auto target_index = std::exchange(target.c.index, source);
            DUMP("try backup load");
            return Load::call(target_index, target, p, t);
        }
    }
}

inline Load::stat Ref::load_to(Target &target) noexcept {
    DUMP("load_to", has_value(), name(), target.name(), pointer().base);
    if (!has_value()) return Load::None;
    return dump_or_load(target, index(), pointer(), tag());
}

template <class T, std::enable_if_t<!std::is_reference_v<T>, int>>
std::optional<T> Ref::load(Type<T>) {
    std::optional<T> out;
    if (!has_value()) {
        DUMP("no value");
        // nothing
    } else if (index().equals<T>()) {
        DUMP("load exact match");
        switch (tag()) {
            case Tag::Stack:   {if constexpr(std::is_constructible_v<T, T&&>) {out.emplace(pointer().load<T &&>()); destroy_if_managed();} break;}
            case Tag::Heap:    {if constexpr(std::is_constructible_v<T, T&&>) {out.emplace(pointer().load<T &&>()); destroy_if_managed();} break;}
            case Tag::Const:   {if constexpr(is_copy_constructible_v<T>) out.emplace(pointer().load<T const &>()); break;}
            case Tag::Mutable: {if constexpr(is_copy_constructible_v<T>) out.emplace(pointer().load<T &>()); break;}
        }
    } else {
        storage_like<T> storage;
        Target target(Index::of<T>(), &storage, sizeof(storage), Target::constraint<T>);
        switch (load_to(target)) {
            case Load::Stack: {
                DUMP("load succeeded");
                DestructGuard<T, false> raii{storage_cast<T>(storage)};
                out.emplace(std::move(raii.held));
                break;
            }
            case Load::Heap: {
                DUMP("load succeeded");
                DestructGuard<T, true> raii{*static_cast<T *>(target.output())};
                out.emplace(std::move(raii.held));
                break;
            }
            case Load::Exception: {target.rethrow_exception();}
            case Load::OutOfMemory: {throw std::bad_alloc();}
            default: {}
        }
    }
    return out;
}

template <class T, std::enable_if_t<std::is_reference_v<T>, int>>
std::remove_reference_t<T> * Ref::load(Type<T>) const {
    DUMP("load reference", type_name<T>(), name());
    if (auto t = target<T>()) return t;
    return nullptr;
}

/******************************************************************************************/

}
