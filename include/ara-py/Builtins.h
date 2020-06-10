#pragma once
#include "Load.h"
#include "Variable.h"

namespace ara::py {

union pyNone;
union pyBool;
union pyInt;
union pyFloat;
union pyStr;
union pyBytes;
union pyFunction;
union pyList;
union pyUnion;
union pyOption;
union pyDict;
union pyTuple;
union pyMemoryView;

/******************************************************************************/

union pyNone {
    using builtin = PyObject;
    PyObject object;

    static Always<pyType> def() {return *Py_None->ob_type;}

    static bool matches(Always<pyType> p) {return +p == Py_None->ob_type;}

    static bool check(Always<> o) {return ~o == Py_None;}

    static Value<pyNone> load(Ignore, Ignore, Ignore) {return {*Py_None, true};}
};

/******************************************************************************/

union pyBool {
    using builtin = PyObject;
    PyObject object;

    static Always<pyType> def() {return PyBool_Type;}

    static bool check(Always<> o) {return PyBool_Check(+o);}
    static bool matches(Always<pyType> p) {return +p == &PyBool_Type;}

    static Value<pyBool> from(bool b) {return {b ? Py_True : Py_False, true};}
    // static Value<Bool> from(ara::Bool b) {return from(static_cast<bool>(b));}

    static Value<pyBool> load(Ref &ref, Ignore, Ignore) {
        DUMP("load_bool");
        if (auto p = ref.get<Bool>()) return from(bool(*p));
        return {};//return type_error("could not convert to bool");
    }
};


/******************************************************************************/

union pyInt {
    using builtin = PyObject;
    PyObject object;

    static Always<pyType> def() {return PyLong_Type;}

    static bool check(Always<> o) {return PyLong_Check(+o);}
    static bool matches(Always<pyType> p) {return +p == &PyLong_Type;}

    static Value<pyInt> from(Integer i) {return Value<pyInt>::from(PyLong_FromLongLong(static_cast<long long>(i)));}

    static Value<pyInt> load(Ref &ref, Ignore, Ignore) {
        DUMP("loading int");
        if (auto p = ref.get<Integer>()) return from(*p);
        DUMP("cannot load int");
        return {};
    }
};

/******************************************************************************/

union pyFloat {
    using builtin = PyObject;
    PyObject object;

    static Always<pyType> def() {return PyFloat_Type;}

    static bool check(Always<> o) {return PyFloat_Check(+o);}
    static bool matches(Always<pyType> p) {return +p == &PyFloat_Type;}

    static Value<pyFloat> from(Float x) {return Value<pyFloat>::from(PyFloat_FromDouble(x));}

    static Value<pyFloat> load(Ref &ref, Ignore, Ignore) {
        if (auto p = ref.get<Float>()) return from(*p);
        if (auto p = ref.get<Integer>()) return from(static_cast<double>(*p));
        DUMP("bad float");
        return {};
    }
};

/******************************************************************************/

union pyStr {
    using builtin = PyObject;
    PyObject object;

    static Always<pyType> def() {return PyUnicode_Type;}

    static bool check(Always<> o) {return PyUnicode_Check(+o);}
    static bool matches(Always<pyType> p) {return +p == &PyUnicode_Type;}

    // static PyUnicodeObject* get(Always<> o) {
    //     return PyUnicode_Check(+o) ? reinterpret_cast<PyUnicodeObject *>(+o) : nullptr;
    // }

    static Value<pyStr> from(Str s) {return Value<pyStr>::from(PyUnicode_FromStringAndSize(s.data(), s.size()));}

    static Value<pyStr> load(Ref &ref, Ignore, Ignore) {
        DUMP("converting", ref.name(), " to str");
        if (auto p = ref.get<Str>()) return from(std::move(*p));
        if (auto p = ref.get<String>()) return from(std::move(*p));

        // if (auto p = ref.get<std::wstring_view>())
        //     return {PyUnicode_FromWideChar(p->data(), static_cast<Py_ssize_t>(p->size())), false};
        // if (auto p = ref.get<std::wstring>())
        //     return {PyUnicode_FromWideChar(p->data(), static_cast<Py_ssize_t>(p->size())), false};
        return {};
    }
};

/******************************************************************************/

union pyBytes {
    using builtin = PyObject;
    PyObject object;

    static bool check(Always<> o) {return PyBytes_Check(+o);}
    static bool matches(Always<pyType> p) {return +p == &PyBytes_Type;}

    static Value<pyBytes> load(Ref &ref, Ignore, Ignore) {
        // if (auto p = ref.get<BinaryView>()) return as_object(std::move(*p));
        // if (auto p = ref.get<Binary>()) return as_object(std::move(*p));
        return {};
    }
};

/******************************************************************************/

inline std::string_view as_string_view(Always<pyStr> o) {
    Py_ssize_t size;
#if PY_MAJOR_VERSION > 2
    char const *c = PyUnicode_AsUTF8AndSize(~o, &size);
#else
    char *c;
    if (PyString_AsStringAndSize(~o, &c, &size)) throw PythonError();
#endif
    if (!c) throw PythonError();
    return std::string_view(static_cast<char const *>(c), size);
}

/******************************************************************************/

inline std::string_view as_string_view(Always<pyBytes> o) {
    char *c;
    Py_ssize_t size;
    PyBytes_AsStringAndSize(~o, &c, &size);
    if (!c) throw PythonError();
    return std::string_view(c, size);
}

/******************************************************************************/

template <class T>
inline std::string_view as_string_view(T t) {
    if (auto s = t.template get<pyStr>()) return as_string_view(*s);
    if (auto s = t.template get<pyBytes>()) return as_string_view(*s);
}

/******************************************************************************/

template <class T>
inline std::ostream& operator<<(std::ostream& os, Ptr<T> const& o) {
    if (!o) return os << "null";
    auto obj = Value<pyStr>::from(PyObject_Str(+o));
    if (auto u = get_unicode(*obj)) return os << as_string_view(*u);
    throw PythonError(type_error("Expected str"));
}

/******************************************************************************/


// union IndexType {
//     static bool check(Always<> o) {return PyObject_TypeCheck(+o, +static_type<Index>());}
//     static bool matches(Always<pyType> p) {return p == static_type<IndexType>();}

//     static Value<IndexType> load(Ref &ref, Ignore, Ignore) {
//         if (auto p = ref.get<Index>()) return as_object(std::move(*p));

//         // auto c1 = r.name();
//         // auto c2 = p->name();
//         // throw PythonError(type_error("could not convert object of def %s to def %s", c1.data(), c2.data()));

//         else return {};
//     }
// };

union pyFunction {
    using builtin = PyObject;
    PyObject object;

    static bool matches(Always<pyType> p) {return +p == &PyFunction_Type;}
    static bool check(Always<> p) {return PyFunction_Check(+p);}

    static Value<pyFunction> load(Ref &ref, Ignore, Ignore) {
        // if (auto p = ref.get<Function>()) return as_object(std::move(*p));
        // if (auto p = ref.get<Overload>()) return as_object(Function(std::move(*p)));
        return {};
    }
};

union pyMemoryView {
    using builtin = PyObject;
    PyObject object;

    static bool matches(Always<pyType> p) {return +p == &PyMemoryView_Type;}
    static bool check(Always<> p) {return PyMemoryView_Check(+p);}

    static Value<pyMemoryView> load(Ref &ref, Value<> const &root) {
        // if (auto p = ref.get<ArrayView>()) {
        //     auto x = TypePtr::from<ArrayBuffer>();
        //     auto obj = Value<>::from(PyObject_CallObject(x, nullptr));
        //     cast_object<ArrayBuffer>(obj) = {std::move(*p), root};
        //     return Value<>::from(PyMemoryView_FromObject(obj));
        // }
        return {};
    }
};

/******************************************************************************/

bool is_structured_type(Always<> def, PyTypeObject *origin) {
    if (+def == reinterpret_cast<PyObject*>(origin)) return true;
    if constexpr(Version >= decltype(Version)(3, 7, 0)) {
        Value<> attr(PyObject_GetAttrString(+def, "__origin__"), false);
        return reinterpret_cast<PyObject *>(origin) == +attr;
    } else {
        // case like typing.pyTuple: issubclass(typing.pyTuple[int, float], tuple)
        // return is_subclass(reinterpret_cast<PyTypeObject *>(def), reinterpret_cast<PyTypeObject *>(origin));
    }
    return false;
}

union pyList {
    using builtin = PyObject;
    PyObject object;

    static bool matches(Always<> p) {return is_structured_type(p, &PyList_Type);}

    static Value<> load(Ref &ref, Always<> p, Value<> root) {
        // Load Array.
        // For each, load value def.
        return {};
    }
};

union pyDict {
    using builtin = PyObject;
    PyObject object;

    static bool matches(Always<> p) {return is_structured_type(p, &PyDict_Type);}

    static Value<> load(Ref &ref, Always<> p, Value<> root) {
        DUMP("loading pyDict[]", ref.name());
        if (auto a = ref.get<Array>()) {
            Span &s = *a;
            if (s.rank() == 1) {
                auto out = Value<>::from(PyDict_New());
                s.map([&](Ref &r) {
                    if (auto v = r.get<View>()) {
                        if (v->size() != 2) return false;
                        Value<> key, value;
                        PyDict_SetItem(+out, +key, +value);
                        return true;
                    }
                    return false;
                });
            } else if (s.rank() == 2 && s.length(1) == 2) {
                auto out = Value<>::from(PyDict_New());
                Value<> key;
                s.map([&](Ref &r) {
                    if (key) {
                        auto value = Value<>::from(PyDict_New());
                        PyDict_SetItem(+out, +key, +value);
                        key = {};
                    } else {
                        // key = r.get<>
                    }
                    return true;
                });
            }
        }
        // -
        // strategy: load Array --> gives pair<K, V>[n] --> then load each value as View --> then load each element as Key, Value
        // strategy: load pyTuple --> gives Ref[n] --> then load each ref to a View --> then load each element as Key, Value
        // these are similar... main annoyance is the repeated allocation for a 2-length View...
        // strategy: load Array --> gives variant<K, V>[n, 2] --> then load each value. hmm. not great for compile time. // bad
        // hmm, this seems unfortunate. Maybe pyTuple[] should actually have multiple dimensions?:
        // then: load pyTuple --> gives Ref[n, 2] --> load each element as Key, Value
        // problem with this is that load pyTuple, should it return ref(pair)[N] or ref[N, 2] ... depends on the context which is better.
        // other possibility is to just declare different dimension types ... sigh, gets nasty.
        // other alternative is to make a map def which is like pyTuple[N, 2].
        return {};
    }
};

union pyTuple {
    using builtin = PyObject;
    PyObject object;
    static bool matches(Always<> p) {return is_structured_type(p, &PyTuple_Type);}

    static Value<> load(Ref &ref, Always<> p, Value<> root) {
        // load pyTuple or View, go through and load each def. straightforward.
        return {};
    }

    Always<> item(Py_ssize_t i) {return *PyTuple_GET_ITEM(&object, i);}
    auto size() const {return PyTuple_GET_SIZE(&object);}
};

union pyUnion {
    using builtin = PyObject;
    PyObject object;
    static bool matches(Always<> p) {return false;}// is_structured_type(p, &PyUnion_Type);}

    static Value<> load(Ref &ref, Always<> p, Value<> root) {
        // try loading each possibility. straightforward.
        return {};
    }
};

union pyOption {
    using builtin = PyObject;
    PyObject object;
    static bool matches(Always<> p) {return false;}// is_structured_type(p, &PyUnion_Type);}

    static Value<> load(Ref &ref, Always<> p, Value<> root) {
        // if !ref return none
        // else return load .. hmm needs some thinking
        return {};
    }
};


/******************************************************************************/

// template <class T, class S>
// Maybe<T> instance(Always<S> s) {
//     return T::check(s) ? s : nullptr;
// }

// template <class T, class S>
// Always<T> instance_cast(Always<S> s) {
//     return T::check(s) ? s : throw PythonError::type("bad cast");
// }

/******************************************************************************/

template <class F>
Value<> map_output(Always<> t, F &&f);// {
    // Type objects
//     if (auto def = instance<Type>(t)) {
//         if (pyNone::matches(*def))  return f(pyNone());
//         if (pyBool::matches(*def))  return f(Bool());
//         if (pyInt::matches(*def))   return f(pyInt());
//         if (pyFloat::matches(*def)) return f(Float());
//         if (pyStr::matches(*def))   return f(Str());
//         if (pyBytes::matches(*def)) return f(Bytes());
//         // if (IndexType::matches(*def)) return f(IndexType());
// // //         else if (*def == &PyBaseObject_Type)                  return as_deduced_object(std::move(r));        // object
//         // if (*def == static_type<VariableType>()) return f(VariableType());           // Value
//     }
//     // Non def objects
//     DUMP("Not a def");
//     // if (auto p = instance<Index>(t)) return f(Index());  // Index

//     if (pyUnion::matches(t)) return f(pyUnion());
//     if (pyList::matches(t))  return f(pyList());       // pyList[T] for some T (compound def)
//     if (pyTuple::matches(t)) return f(pyTuple());      // pyTuple[Ts...] for some Ts... (compound def)
//     if (pyDict::matches(t))  return f(pyDict());       // pyDict[K, V] for some K, V (compound def)
//     DUMP("Not one of the structure types");
    // return Value<>();
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
// }

}