
#include <ara/Call.h>
#include <ara/Core.h>
#include <ara-cpp/Schema.h>
#include <ara-cpp/Standard.h>
#include <ara-cpp/Array.h>
#include <ara-cpp/Tuple.h>
#include <iostream>


#include <string_view>
#include <vector>
#include <string>
#include <any>
#include <mutex>

static_assert(16 == sizeof(std::shared_ptr<int>));
static_assert(16 == sizeof(std::string_view));
static_assert(24 == sizeof(std::string));
static_assert(32 == sizeof(std::any));
static_assert(24 == sizeof(std::vector<int>));
static_assert(8 == sizeof(std::unique_ptr<int>));
static_assert(16 == sizeof(std::shared_ptr<int>));
static_assert(8 == sizeof(std::optional<int>));

static_assert(std::is_trivially_copyable_v<std::optional<int>>);


using ara::Type;
using ara::Value;
using ara::Schema;

/******************************************************************************/

struct GooBase {

};

//remove iostream
struct Goo : GooBase {
    double x=1000;
    std::string name = "goo";

    Goo(double xx) : x(xx) {}
    Goo(Goo const &g) : x(g.x) {DUMP("copy");}
    Goo(Goo &&g) noexcept : x(g.x) {DUMP("move"); g.x = -1;}

    Goo & operator=(Goo g) {
        x = g.x;
        DUMP("assign");
        return *this;
    }

    void test_throw(double xx) {
        if (xx < 0) throw std::runtime_error("cannot be negative");
        else x += xx;
    }

    friend std::ostream &operator<<(std::ostream &os, Goo const &g) {
        return os << "Goo(" << g.x << ", " << &g.x << ")";
    }
};

// bool members(Self<Goo> &s) {
//     return s("x", &Goo::x);
// }


// template <class T>
// struct Temporary {
//     T &&t;
//     T *operator->() const {return &t;}
//     T &&operator*() const {return t;}
// };

// String
// --> Ref holding <String &&>
// now we want String back
// ...mm not really possible
// in this case we need Value to be possible as an argument
// what about excluding && possibility?
// i.e. in c++ if we want to move in we instead allocate a moved version of the input
// obvious problem is the allocation, first of all. not really necessary.
// ok so then we have all 4 possibilties again I guess.
// request<T> is still fairly easy I think
// &T, &mut T, T
// T&, T const &, T &&
// response is fairly wordy
// bool to_value(Object &o, Index i, Thing t) {
//     if (i.equals<std::string>()) {
//         if (auto r = t.rvalue()) return o.emplace<std::string>((*r).name);
//         return o.emplace<std::string>((*r).name);
//     }

//     if (i.equals<GooBase>()) {
//         if (auto r = t.rvalue()) o.emplace(*r);
//         else o.emplace(*t);
//     }

//     if (t.in_scope()) {
//         return t.member(&M::name)
//             || t.derive<GooBase>()
//             || (i.equals<std::string_view>() && o.emplace(r->name));
//             || (i.equals<mutable_string_view>()) && o.visit<mutable_string_view>() {
//             if (auto r = t.lvalue()) return (*r).name;
//         }

        // if (i.equals<std::string const &>())
        //     return o.emplace<std::string>(r->name);

        // if (i.equals<std::string &&>())
        //     if (auto r = t.rvalue()) return o.emplace<std::string>((*r).name);

        // if (i.equals<std::string &>())
        //     if (auto r = t.lvalue()) return o.emplace<std::string>((*r).name);


        // if (i.equals<GooBase &>())
        //     if (auto r = t.lvalue()) return o.emplace(*r);
        // if (i.equals<GooBase const &>())
        //     if (auto r = t.lvalue()) return o.emplace(*r);
        // if (i.equals<GooBase &&>())
        //     if (auto r = t.lvalue()) return o.emplace(*r);
//     }
// }


// // maybe better to just use this in the non-reference case:
// // these are safe conversions even if g is destructed later
// bool to_value(Object &o, Index i, Temporary<Goo const> g) {
//     // inserts copy
//     if (i.equals<std::string>()) return o.emplace<std::string>(t->x), true; // safe
// }

// // this safe too, yep
// bool to_value(Object &o, Index i, Temporary<Goo> g) {
//     if (i.equals<std::string>()) return o.emplace<std::string>(std::move(g->name)); // safe
//     return to_value<Temporary<Goo const>>();
// }

// // almost same as Temporary<Goo>...but can bind to a const & too I guess.
// bool to_value(Object &o, Index i, Goo &&) {
//     // if (i.equals<std::string>()) return o.emplace<std::string>(std::move(g->name)); // safe
//     return to_value<Temporary<Goo>>() || to_value<Goo const &>();
// }

// bool to_value(Object &o, Index i, Goo &) {
//     // not particularly safe...but allowable in the function call context
//     if (i.equals<mutable_view>()) return o.emplace<mutable_view>(g->name); // bad
//     return to_value<Goo const &>(); // what about a temporary &? not sure any use
// }


// bool to_value(Object &o, Index i, Goo const &g) {
//     if (i.equals<std::string_view>()) return o.emplace<std::string_view>(g.name), true; // safe
//     return to_value<Temporary<Goo const>>();
// }


// we'll probably take out "guaranteed" copy constructibility
// std::optional<Goo> from_ref(Object &r, Type<Goo>) {
//     std::optional<Goo> out;
//     // move_as does the request and destroys itself in event of success
//     // some of these would not need to destroy themselves though
//     // (i.e.) if request is for a reference or a trivially_copyable type
//     // maybe it is better to just say double *...?
//     if (auto t = r.move_as<double>()) out.emplace(std::move(*t));
//     if (auto t = r.move_as<double &>()) out.emplace(*t);
//     if (auto t = r.move_as<double const &>()) out.emplace(*t);
//     // not entirely sure what point of this one is...
//     // well, I guess it is useful for C++ code.
//     if (auto t = r.move_as<double &&>()) out.emplace(std::move(*t));
//     return out;
// }

/******************************************************************************/

template <>
struct ara::Impl<GooBase> : ara::Default<GooBase> {
    static bool method(Frame, GooBase const &) {return false;}
};

template <>
struct ara::Impl<Goo> : ara::Default<Goo> {
    static auto attribute(Target& target, Goo const& self, std::string_view name) {
        if (name == "x") return target.assign(self.x); // change to mem fn
        return false;
    }

    // static bool call(Frame body) {
    //     return body("new", [](double d) {return Goo(d);});
    // }

    static bool method(Frame body, Goo &self) {
        DUMP("calling Goo");
        return body(self, ".x", &Goo::x, {0})
            || body(self, ".x=", [](Goo &s, double x) {s.x = x;});
    }

    static bool method(Frame, Goo &&) {
        DUMP("calling Goo");
        return false;
    }

    static bool method(Frame body, Goo const &self) {
        DUMP("calling Goo body", ara::Lifetime({0}).value);
        return body(self, &Goo::x)
            || body(self, "add", [](Goo g, Goo g2, Goo g3) {return g;})
            // || body(self, "test_throw", &Goo::test_throw)
            || body(self, "get_x", [](Goo const &g) {return g.x;})
            // || body(self, "add", [](Goo &g, double x) {g.x += x;})
            || body.derive<GooBase const &>(self);
    }
};


/******************************************************************************/

// could make this return a schema
Schema make_schema() {
    Schema s;
    s.object("global_value", 123);
    s.function("easy", [] {
        DUMP("invoking easy");
        return 1.2;
    });
    s.function("fun", [](int i, double d) {
        DUMP("fun", i, d);
        return i + d;
    });
    s.function("refthing", [](double const &d) {
        return d;
    });
    s.function("submodule.fun", [](int i, double d) {
        return i + d;
    });
    s.function("test_pair", [](std::pair<int, double> p) {
        p.first += 3;
        p.second += 0.5;
        return p;
    });
    s.function("test_tuple", [](std::tuple<int, float> p) {
        return std::get<1>(p);
    });
    s.function("vec", [](double i, double d) {
        return std::vector<double>{i, i, d};
    });
    s.function("moo", [](Goo &i) {
        i.x += 5;
    });
    s.function("lref", [](double &i) {i = 2;});
    s.function("lref2", [](double &i) -> double & {return i = 2;});
    s.function("lref3", [](double const &i) -> double const & {return i;});
    s.function("clref", [](double const &i) {});
    s.function("noref", [](double i) {});
    s.function("rref", [](double &&i) {});
    s.function("str_argument", [](std::string_view s) {DUMP("string =", s); return std::string(s);});
    s.function("string_argument", [](std::string s) {DUMP("string =", s); return s;});
    s.function("stringref_argument", [](std::string const &s) {DUMP("string =", s); return s;});

    s.object("Goo", ara::Index::of<Goo>());

    // s.function("buffer", [](std::tuple<ara::BinaryData, std::type_index, std::vector<std::size_t>> i) {
    //     DUMP(std::get<0>(i).size());
    //     DUMP(std::get<1>(i).name());
    //     DUMP(std::get<2>(i).size());
    //     for (auto &c : std::get<0>(i)) c += 4;
    // });
    s.function("vec1", [](std::vector<int> const &) {});
    s.function("vec2", [](std::vector<int> &) {});
    s.function("vec3", [](std::vector<int>) {});
    s.function("mutex", [] {return std::mutex();});
    s.function("bool", [](bool b) {return b;});
    s.function("dict", [](std::map<std::string, std::string> b) {
        DUMP("map size", b.size());
        for (auto const &p : b) DUMP("map item", p.first, p.second);
        return b;
    });
    // s.function<1>("vec4", [](int, int i=2) {});

    DUMP("made schema");
    return s;
}

struct BootKey;
using Boot = BootKey*;

template <>
struct ara::Impl<Boot> : ara::Default<Boot> {
    static bool method(ara::Frame body, Boot const &self) {
        return body(self, [](Boot const &self) {
            return make_schema();
        });
    }
};


ARA_DECLARE(example_boot, Boot);
ARA_DEFINE(example_boot, Boot);


