#pragma once
#include "Raw.h"

namespace ara::py {

/******************************************************************************/

inline Str from_unicode(Instance<PyUnicodeObject> o) {
    Py_ssize_t size;
#if PY_MAJOR_VERSION > 2
    char const *c = PyUnicode_AsUTF8AndSize(o.object(), &size);
#else
    char *c;
    if (PyString_AsStringAndSize(o.object(), &c, &size)) throw PythonError();
#endif
    if (!c) throw PythonError();
    return Str(static_cast<char const *>(c), size);
}

/******************************************************************************/

inline Str from_bytes(Instance<PyBytesObject> o) {
    char *c;
    Py_ssize_t size;
    PyBytes_AsStringAndSize(o.object(), &c, &size);
    if (!c) throw PythonError();
    return Str(c, size);
}

/******************************************************************************/

template <class T>
bool dump_arithmetic(Target &target, Instance<> o) {
    DUMP("cast arithmetic in:", target.name());
    if (PyFloat_Check(+o)) return target.emplace_if<T>(PyFloat_AsDouble(+o));
    if (PyLong_Check(+o))  return target.emplace_if<T>(PyLong_AsLongLong(+o));
    if (PyBool_Check(+o))  return target.emplace_if<T>(+o == Py_True);
    if (PyNumber_Check(+o)) { // This can be hit for e.g. numpy.int64
        if (std::is_integral_v<T>) {
            if (auto i = Shared::from(PyNumber_Long(+o)))
                return target.emplace_if<T>(PyLong_AsLongLong(+i));
        } else {
            if (auto i = Shared::from(PyNumber_Float(+o)))
               return target.emplace_if<T>(PyFloat_AsDouble(+i));
        }
    }
    DUMP("cast arithmetic out:", target.name());
    return false;
}

inline PyUnicodeObject* get_unicode(Instance<> o) {
    return PyUnicode_Check(+o) ? reinterpret_cast<PyUnicodeObject *>(+o) : nullptr;
}

inline PyBytesObject* get_bytes(Instance<> o) {
    return PyBytes_Check(+o) ? reinterpret_cast<PyBytesObject *>(+o) : nullptr;
}

/******************************************************************************/

inline bool dump_object(Target &target, Instance<> o) {
    DUMP("dumping object");

    if (auto v = cast_if<Variable>(+o)) {
        auto acquired = acquire_ref(*v, LockType::Read);
        return acquired.ref.load_to(target);
    }

    if (target.accepts<Str>()) {
        if (auto p = get_unicode(o)) return target.emplace_if<Str>(from_unicode(instance(p)));
        if (auto p = get_bytes(o)) return target.emplace_if<Str>(from_bytes(instance(p)));
        return false;
    }

    if (target.accepts<String>()) {
        if (auto p = get_unicode(o)) return target.emplace_if<String>(from_unicode(instance(p)));
        if (auto p = get_bytes(o)) return target.emplace_if<String>(from_bytes(instance(p)));
        return false;
    }

    if (target.accepts<Index>()) {
        if (auto p = cast_if<Index>(+o)) return target.set_if(*p);
        else return false;
    }

    if (target.accepts<Float>())
        return dump_arithmetic<Float>(target, o);

    if (target.accepts<Integer>())
        return dump_arithmetic<Integer>(target, o);

    if (target.accepts<bool>()) {
        if ((+o)->ob_type == Py_None->ob_type) { // fix, doesnt work with Py_None...
            return target.set_if(false);
        } else return dump_arithmetic<bool>(target, o);
    }

    return false;
}

/******************************************************************************/

}

namespace ara {

template <>
struct Dumpable<py::Export> {
    bool operator()(Target &v, py::Export const &o) const {return false;}

    bool operator()(Target &v, py::Export &o) const {
        DUMP("dumping object!");
        return py::dump_object(v, py::instance(reinterpret_cast<PyObject*>(&o)));
    }
};

}