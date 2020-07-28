
#include <ara-cpp/Schema.h>
#include <ara/Core.h>
#include <mutex>

struct Example;

namespace ara {

template <>
ara_stat Switch<Example>::invoke(ara_input, void*, void*, void*) noexcept;

}

using Mod = ara::Module<Example>;

int main() {
    using namespace ara;
    Mod::init();

    // auto v = schema["global_value"];
    // auto p = v.target<int const &>();
    // DUMP(v.name(), *p);
    // DUMP(v.has_value());
    // DUMP(v.get<int>(s).value());
{
    DUMP("testing first function fun with args 1, 2.0");
    auto blah = Mod::call<double>("fun", Caller(), 1, 2.0);
    DUMP("got result", blah);
}
{
    DUMP("call void");
    Mod::call<void>("fun", Caller(), 1, 2.0);
}
{
    DUMP("call Value");
    auto val = Mod::call<Value>("fun", Caller(), 1, 2.0);
}
{
    DUMP("call void 2");
    double x;
    Mod::call<void>("lref", Caller(), x);
}
{
    DUMP("call ref");
    double x;
    double &y = Mod::call<double &>("lref2", Caller(), x);
    DUMP(x, y, &x == &y);
}

{
    DUMP("call ref2");
    double x=5;
    double const &y = Mod::call<double const &>("lref3", Caller(), x);
    DUMP(bool(y));
    DUMP(x, y, &x == &y);
}
    std::mutex mut;
    try {
        auto x = Mod::call<Value>("mutex", Caller(), mut);
    } catch (...) {

    }

    auto x = Mod::call<Value, false>("mutex", Caller(), mut);

{
    auto v = Value(1);
    DUMP(bool(v.get<int>()));
    DUMP(bool(v.get<double>()));
}

{
    DUMP("Goo stuff");
    Value v = Mod::call<Value>("Goo.new", Caller(), 1.5);
    DUMP("got the Goo?", v.name(), int(v.location()));//, int(Value::loc_of<Goo>));

    double y = v.method<double, 1>(Caller(), ".x");
    DUMP("got .x", y);
    double y2 = v.method<double, 1>(Caller(), ".x");
    DUMP("got .x", y2);
    double y3 = v.method<double>(Caller());
}

    DUMP("done");
    return 0;
}