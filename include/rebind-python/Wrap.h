#pragma once
#include "Object.h"
#include "API.h"
#include <rebind/Value.h>
#include <rebind/Conversions.h>

namespace rebind {

/******************************************************************************/

template <class T>
struct Wrap {
    static PyTypeObject type;
    PyObject_HEAD // 16 bytes for the ref count and the type object
    T value; // stack is OK because this object is only casted to anyway.
};

template <class T>
PyTypeObject Wrap<T>::type;

template <class T>
SubClass<PyTypeObject> type_object(Type<T> t={}) {return {&Wrap<T>::type};}

/******************************************************************************/

/// Main wrapper type for Value: adds a ward object for lifetime management
struct PyValue : Value {
    using Value::Value;
    Object ward = {};
};

/// Main wrapper type for Pointer: adds a ward object for lifetime management
struct PyPointer : Pointer {
    using Pointer::Pointer;
    Object ward = {};
};

template <>
struct Wrap<Value> : Wrap<PyValue> {};

template <>
struct Wrap<Pointer> : Wrap<PyPointer> {};

/******************************************************************************/

template <class T>
T * cast_if(PyObject *o) {
    if (!PyObject_TypeCheck(o, type_object<T>())) return nullptr;
    return std::addressof(reinterpret_cast<Wrap<T> *>(o)->value);
}

template <class T>
T & cast_object(PyObject *o) {
    if (!PyObject_TypeCheck(o, type_object<T>()))
        throw std::invalid_argument("Expected instance of rebind.TypeIndex");
    return reinterpret_cast<Wrap<T> *>(o)->value;
}

/******************************************************************************/

template <class T>
PyObject *tp_new(PyTypeObject *subtype, PyObject *, PyObject *) noexcept {
    static_assert(noexcept(T{}), "Default constructor should be noexcept");
    PyObject *o = subtype->tp_alloc(subtype, 0); // 0 unused
    if (o) new (&cast_object<T>(o)) T; // Default construct the C++ type
    return o;
}

/******************************************************************************/

template <class T>
void tp_delete(PyObject *o) noexcept {
    reinterpret_cast<Wrap<T> *>(o)->~Wrap<T>();
    Py_TYPE(o)->tp_free(o);
}

/******************************************************************************/

template <class T>
PyObject * copy_from(PyObject *self, PyObject *other) noexcept {
    return raw_object([=] {
        cast_object<T>(self) = cast_object<T>(other); // not notexcept
        return Object(Py_None, true);
    });
}

/******************************************************************************/

template <class T>
PyTypeObject type_definition(char const *name, char const *doc) {
    PyTypeObject o{PyVarObject_HEAD_INIT(NULL, 0)};
    o.tp_name = name;
    o.tp_basicsize = sizeof(Wrap<T>);
    o.tp_dealloc = tp_delete<T>;
    o.tp_new = tp_new<T>;
    o.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
    o.tp_doc = doc;
    return o;
}

/******************************************************************************/

template <>
struct Response<Object> {
    Value operator()(TypeIndex t, Object o) const {
        Value v;
        DUMP("trying to get Value from Object", t);
        // if (auto p = cast_if<Pointer>(o)) {
        //     DUMP("requested qualified variable", t, p->index());
        //     v = p->request_value(t);
        //     DUMP(p->index(), t, v.index());
        // }
        return v;
    }
};

}
