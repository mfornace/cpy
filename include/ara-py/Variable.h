#pragma once
#include "Index.h"
#include "Call.h"
#include <ara/Ref.h>
#include <atomic>

namespace ara::py {

/******************************************************************************/

struct Variable {
    struct Address {
        void* pointer;
        Mode qualifier; // Heap, Read, or Write
    };

    union Storage {
        Address address;
        char data[24];
    };

    union Lock {
        std::uintptr_t count;
        // only necessary when lock is empty, otherwise that Variable's lock would be used.
        // 0: no references
        // 1: one const reference
        // 2: two const references
        // <0: one mutable reference
        Always<> other;
        // empty: no lock
        // otherwise reference to another Variable
        // may be tuple of Variables? not sure yet.
        constexpr Lock() noexcept : count(0) {}
    };

    enum State : ara_mode {Stack, StackAlias, Heap, HeapAlias};

    template <class T>
    static constexpr bool allocated = is_stackable<T>(sizeof(Storage));

    Tagged<State> idx;
    Storage storage; // stack storage
    Lock lock;

    Variable() noexcept = default;
    ~Variable() noexcept {reset();}

    Variable(Variable const &) = delete;
    Variable(Variable &&) = delete; //default;

    void set_stack(Index i) noexcept {
        this->lock.count = 0;
        idx = Tagged<State>(i, Stack);
    }

    void set_pointer(Index i, void *ptr, Mode tag) noexcept {
        storage.address.pointer = ptr;
        storage.address.qualifier = tag;
        this->lock.count = 0;
        idx = Tagged<State>(i, Heap);
    }

    std::uintptr_t & current_lock();

    void set_lock(Always<> other);

    bool has_value() const noexcept {return idx.has_value();}

    Index index() const noexcept {return Index(idx);}

    void reset() noexcept;

    auto name() const noexcept {return index().name();}

    void *address() const {return idx.tag() < 2 ? const_cast<Storage *>(&storage) : storage.address.pointer;}

    static constexpr auto Mutating = std::numeric_limits<std::uintptr_t>::max();

    Ref begin_acquisition(LockType type) {
        if (!has_value()) return Ref();
        auto &count = current_lock();

        if (count == Mutating) {
            throw PythonError::type("cannot reference object which is being mutated");
        } else if (count == 0 && type == LockType::Write) {
            DUMP("write lock");
            count = Mutating;
            return Ref(index(), Mode::Write, Pointer::from(address()));
        } else if (type == LockType::Read) {
            DUMP("read lock");
            ++count;
            return Ref(index(), Mode::Read, Pointer::from(address()));
        } else {
            throw PythonError::type("cannot mutate object which is already referenced");
        }
    }

    void end_acquisition() {
        if (!has_value()) return;
        auto &count = current_lock();

        if (count == Mutating) count = 0;
        else --count;
        DUMP("restored acquisition!", count);
    }

    struct AcquiredRef {
        Ref ref;
        Variable &v;
        ~AcquiredRef() noexcept {v.end_acquisition();}
    };

    AcquiredRef acquire_ref(LockType type) {
        return AcquiredRef{begin_acquisition(type), *this};
    }

};

/******************************************************************************/

struct VariableObject : ObjectBase, Variable {};

Value<> try_load(Ref &r, Always<> t, Maybe<> root);

/******************************************************************************/

struct pyVariable : StaticType<pyVariable> {
    using type = VariableObject;

    static void placement_new(Variable &v) noexcept {new (&v) Variable();}

    static void initialize_type(Always<pyType>) noexcept;

    /**************************************************************************/

    static Value<> return_self(Always<> self, Ignore) noexcept {return self;}

    static Value<pyNone> reset(Always<pyVariable> self, Ignore) {
        self->reset();
        return Always<pyNone>(*Py_None);
    }

    static Value<pyIndex> index(Always<pyVariable> self, Ignore) {
        return Value<pyIndex>::new_from(self->index());
    }

    static Value<pyInt> use_count(Always<pyVariable> self, Ignore) noexcept {
        Integer stat = 0;
        if (self->has_value()) {
            auto i = self->current_lock();
            if (i == Variable::Mutating) stat = -1;
            else stat = i;
        }
        return pyInt::from(stat);
    }

    static int clear(Always<pyVariable> self) noexcept {
        self->reset();
        return 0;
    }

    static int traverse(Always<pyVariable> self, visitproc, void* arg) noexcept {
        return 0;
    }

    /******************************************************************************/

    static int as_bool(Always<pyVariable> self) noexcept {return self->has_value();}

    static Value<> load(Always<pyVariable> self, Always<> type);

    static Value<> as_float(Always<pyVariable> self) {return load(self, pyFloat::def());}

    static Value<> as_int(Always<pyVariable> self) {return load(self, pyInt::def());}

    static int compare(Always<pyVariable> l, Always<> other, int op) noexcept;

    static Value<> call(Always<pyVariable> v, Always<pyTuple> args, CallKeywords&& kws);

    static Value<> element(Always<pyVariable> v, Always<pyTuple> args, CallKeywords&& kws);

    static Value<> attribute(Always<pyVariable> v, Always<pyTuple> args, CallKeywords&& kws);

    /******************************************************************************/

    static Value<> lock(Always<pyVariable> self, Ignore) noexcept {
        if (self->has_value() && self->idx.tag() & 0x1) return self->lock.other;
        else return {Py_None, true};
    }

    /******************************************************************************/

    static Value<pyInt> state(Always<pyVariable> self, Ignore) {
        if (!self->has_value()) {
            return {Py_None, true};
        } else if (self->idx.tag() < 0x2) {
            return pyInt::from(static_cast<Integer>(Mode::Stack));
        } else {
            return pyInt::from(static_cast<Integer>(self->storage.address.qualifier));
        }
    }

    /******************************************************************************/

    static Py_hash_t hash(Always<pyVariable> self) noexcept;

    template <class T>
    static Value<pyVariable> from(T value, Value<> lock) {
        auto v = Value<pyVariable>::new_from();
        if constexpr(Variable::allocated<T>) {
            v->storage.address.pointer = Allocator<T>::heap(std::move(value));
            v->storage.address.qualifier = Mode::Heap;
        } else {
            Allocator<T>::stack(&v->storage.data, std::move(value));
        }
        if (lock) {
            v->lock.other = *lock;
            Py_INCREF(+v->lock.other);
            v->idx = Tagged(Index::of<T>(), Variable::allocated<T> ? Variable::HeapAlias : Variable::StackAlias);
        } else {
            v->lock.count = 0;
            v->idx = Tagged(Index::of<T>(), Variable::allocated<T> ? Variable::Heap : Variable::Stack);
        }
        return v;
    }

    static Value<pyVariable> from(Ref const &r, Value<> ward) {
        DUMP("making reference pyVariable object");
        auto v = Value<pyVariable>::new_from();
        if (r) {
            switch (r.mode()) {
                case Mode::Read: {v->storage.address.qualifier = Mode::Read; break;}
                case Mode::Write: {v->storage.address.qualifier = Mode::Write; break;}
                default: return type_error("cannot create Variable from temporary reference");
            }
            v->storage.address.pointer = r.pointer().base;
            v->idx = Tagged(r.index(), ward ? Variable::HeapAlias : Variable::Heap);
        }
        return v;
    }

    static Value<> method(Always<pyVariable> v, Always<pyTuple> args, CallKeywords &&kws);

    static PyNumberMethods number_methods;
    static PyMethodDef methods[];
};

/******************************************************************************/

inline void Variable::set_lock(Always<> other) {
    lock.other = other;
    Py_INCREF(+other);
    reinterpret_cast<std::uintptr_t &>(idx.base) |= 1;//Tagged<State>(i, idx.state() | 1);
    ++(Always<pyVariable>::from(other)->lock.count);
}

inline Always<> current_root(Always<pyVariable> v) {return (v->idx.tag() & 0x1) ? v->lock.other : v;}

inline std::uintptr_t & Variable::current_lock() {
    return (idx.tag() & 0x1 ? Always<pyVariable>::from(lock.other)->lock.count : lock.count);
}

Value<> load(Always<pyVariable> self, Always<> type);

/******************************************************************************/

Lifetime call_to_variable(Index self, Pointer address, Mode qualifier, ArgView &args);

/******************************************************************************/

template <class F>
Value<> map_output(Always<> t, F &&f) {
    // Type objects
    if (auto type = Maybe<pyType>(t)) {
        if (pyNone::matches(*type))  return f(pyNone());
        if (pyBool::matches(*type))  return f(pyBool());
        if (pyInt::matches(*type))   return f(pyInt());
        if (pyFloat::matches(*type)) return f(pyFloat());
        if (pyStr::matches(*type))   return f(pyStr());
        if (pyBytes::matches(*type)) return f(pyBytes());
        if (pyIndex::matches(*type)) return f(pyIndex());
// //         else if (*type == &PyBaseObject_Type)                  return as_deduced_object(std::move(r));        // object
        // if (*type == static_type<VariableType>()) return f(VariableType());           // Value
    }

    // Non type objects
    DUMP("Not a type");
    // if (auto p = instance<Index>(t)) return f(Index());  // Index

    if (pyUnion::matches(t)) return f(pyUnion());
    if (pyList::matches(t))  return f(pyList());       // pyList[T] for some T (compound def)
    if (pyTuple::matches(t)) return f(pyTuple());      // pyTuple[Ts...] for some Ts... (compound def)
    if (pyOption::matches(t)) return f(pyOption());      // pyTuple[Ts...] for some Ts... (compound def)
    if (pyDict::matches(t))  return f(pyDict());       // pyDict[K, V] for some K, V (compound def)
    DUMP("Not one of the structure types");
    return {};
//     DUMP("custom convert ", output_conversions.size());
//     if (auto p = output_conversions.find(Value<>{t, true}); p != output_conversions.end()) {
//         DUMP(" conversion ");
//         Value<> o;// = ref_to_object(std::move(r), {});
//         if (!o) return type_error("could not cast value to Python object");
//         DUMP("calling function");
// //         // auto &obj = cast_object<Variable>(o).ward;
// //         // if (!obj) obj = root;
//         return Value<>::from(PyObject_CallFunctionObjArgs(+p->second, +o, nullptr));
//     }
}

/******************************************************************************/

Value<> try_load(Ref &r, Always<> t, Maybe<> root) {
    DUMP("try_load", r.name(), "to type", t, "with root", root);
    return map_output(t, [&](auto T) {
        return decltype(T)::load(r, t, root);
    });
}

/******************************************************************************/

Target variable_target(Variable& out) {
    return {Index(), &out.storage, sizeof(out.storage),
        Target::Write | Target::Read | Target::Heap | Target::Trivial |
        Target::Relocatable | Target::MoveNoThrow | Target::Unmovable | Target::MoveThrow};
}

Lifetime invoke_call(Variable &out, Index self, Pointer address, ArgView &args, Mode mode) {
    Target target = variable_target(out);
    auto const stat = Call::call(self, target, address, mode, args);
    DUMP("Variable got stat and lifetime:", stat, target.c.lifetime);

    switch (stat) {
        case Call::None:        break;
        case Call::Stack:       {out.set_stack(target.index()); break;}
        case Call::Read:        {out.set_pointer(target.index(), target.output(), Mode::Read); break;}
        case Call::Write:       {out.set_pointer(target.index(), target.output(), Mode::Write); break;}
        case Call::Heap:        {out.set_pointer(target.index(), target.output(), Mode::Heap); break;}
        case Call::Impossible:  {throw PythonError(type_error("Impossible"));}
        case Call::WrongNumber: {throw PythonError(type_error("WrongNumber"));}
        case Call::WrongType:   {throw PythonError(type_error("WrongType"));}
        case Call::WrongReturn: {throw PythonError(type_error("WrongReturn"));}
        case Call::Exception:   {throw PythonError(type_error("Exception"));}
        case Call::OutOfMemory: {throw std::bad_alloc();}
    }
    DUMP("output:", out.name(), "address:", out.address(), target.c.lifetime, target.lifetime().value);
    return target.lifetime();
}

/******************************************************************************/

template <class I>
Lifetime invoke_access(Variable &out, Index self, Pointer address, I element, Mode mode) {
    Target target = variable_target(out);
    using A = std::conditional_t<std::is_same_v<I, Integer>, Element, Attribute>;
    auto const stat = A::call(self, target, address, element, mode);
    DUMP("Variable got stat and lifetime:", stat, target.c.lifetime);

    switch (stat) {
        case A::None:        break;
        case A::Stack:       {out.set_stack(target.index()); break;}
        case A::Read:        {out.set_pointer(target.index(), target.output(), Mode::Read); break;}
        case A::Write:       {out.set_pointer(target.index(), target.output(), Mode::Write); break;}
        case A::Heap:        {out.set_pointer(target.index(), target.output(), Mode::Heap); break;}
        case A::Exception:   {throw PythonError(type_error("Exception"));}
        case A::OutOfMemory: {throw std::bad_alloc();}
    }
    DUMP("output:", out.name(), "address:", out.address(), target.c.lifetime, target.lifetime().value);
    return target.lifetime();
}

/******************************************************************************/

template <class F>
Lifetime call_to_output(Value<> &out, Always<> output, F&& fun) {
    if (auto v = Maybe<pyVariable>(output)) {
        DUMP("instance of Variable");
        out = {Py_None, true};
        return fun(*v);
    }

    if (auto t = Maybe<pyType>(output)) {
        DUMP("is type");
        if (is_subclass(*t, pyVariable::def())) {
            DUMP("is Variable subclass");
            auto v = Value<pyVariable>::new_from();
            auto life = fun(*v);
            out = std::move(v);
            return life;
        }
    }
#warning "aliasing is a problem here"
    auto v = Value<pyVariable>::new_from();
    fun(*v);
    Ref r(v->index(), Mode::Write, Pointer::from(v->address()));
    if (auto o = try_load(r, output, Value<>())) {out = o; return {};}
    else throw PythonError(type_error("could not load"));
}

/******************************************************************************/

template <class F>
Lifetime call_to_output(Value<> &out, Maybe<> output, F&& fun) {
    if (output) {
        return call_to_output(out, *output, fun);
    } else {
        auto v = Value<pyVariable>::new_from();
        auto life = fun(*v);
        out = std::move(v);
        return life;
    }
}

/******************************************************************************/

struct TupleLock {
    ArgView &view;
    Always<pyTuple> args;
    Py_ssize_t start;

    // Prepare a lock on each argument: v[i] <-- a[s+i]
    // The immediate next step after constructor should be either read_lock() or lock()
    TupleLock(ArgView &v, Always<pyTuple> a, Py_ssize_t s=0) noexcept
        : view(v), args(a), start(s) {for (auto &ref : view) ref.c.mode_index = nullptr;}

    void lock_default() {
        auto s = start;
        for (auto &ref : view) {
            Always<> it = item(args, s++);
            if (auto p = Maybe<pyVariable>(it)) {
                DUMP("its variable!");
                ref = p->begin_acquisition(LockType::Read);
            } else {
                ref = Ref(Index::of<Export>(), Mode::Write, Pointer::from(+it));
            }
        }
    }

    void lock(std::string_view mode) {
        if (mode.empty())
            return lock_default();
        if (mode.size() != view.size())
            throw PythonError(type_error("wrong number of modes"));

        auto s = start;
        auto c = mode.begin();
        for (auto &ref : view) {
            Always<> it = item(args, s++);
            if (auto p = Maybe<pyVariable>(it)) {
                DUMP("its variable!", *c);
                ref = p->begin_acquisition(*c == 'w' ? LockType::Write : LockType::Read);
            } else {
                ref = Ref(Index::of<Export>(), Mode::Write, Pointer::from(+it));
            }
            ++c;
        }
    }

    ~TupleLock() noexcept {
        auto s = start;
        for (auto &ref : view) {
            if (ref.has_value())
                if (auto v = Maybe<pyVariable>(item(args, s)))
                    v->end_acquisition();
            ++s;
        }
    }
};

// struct SelfTupleLock : TupleLock {
//     Variable &self;
//     SelfTupleLock(Variable &s, ArgView &v, Always<pyTuple> a, std::string_view mode) noexcept : TupleLock(v, a, 1), self(s) {
//         char const first = mode.empty() ? 'r' : mode[0];
//         begin_acquisition(self, first == 'w', first == 'r');
//     }

//     void lock(std::string_view mode) {
//         if (mode.size() > 2) {
//            mode.remove_prefix(std::min(mode.size(), std::size_t(2)));
//             if (mode.size() != view.size())
//                 throw PythonError(type_error("wrong number of modes"));
//             lock_arguments(mode);
//         } else {
//             lock_arguments();
//         }
//     }

//     ~SelfTupleLock() noexcept {end_acquisition(self);}
// };

char remove_mode(std::string_view &mode) {
    char const first = mode.empty() ? 'r' : mode[0];
    mode.remove_prefix(std::min(mode.size(), std::size_t(2)));
    return first;
}

/******************************************************************************/

auto call_with_caller(Index self, Pointer address, Mode mode, ArgView& args, CallKeywords const& kws) {
    auto lk = std::make_shared<PythonFrame>(!kws.gil);
    Caller caller(lk);
    args.c.caller_ptr = &caller;

    Value<> out;
    auto life = call_to_output(out, kws.out, [&](Variable& v){
        return invoke_call(v, self, address, args, mode);
    });
    return std::make_pair(out, life);
}

/******************************************************************************/

template <class Module>
Value<> module_call(Ignore, Always<pyTuple> args, CallKeywords &&kws) {
    return pyIndex::call(Switch<Module>::call, args, std::move(kws));
}

/******************************************************************************/

template <class I>
auto access_with_caller(Index self, Pointer address, I element, Mode mode, CallKeywords const& kws) {
    Value<> out;
    auto life = call_to_output(out, kws.out, [&](Variable& v) {
        return invoke_access(v, self, address, element, mode);
    });
    return std::make_pair(out, life);
}

/******************************************************************************/

}