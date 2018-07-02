#include <cpy/Test.h>
#include <cpy/Macros.h>
#include <iostream>
#include <any>

struct goo {
    friend std::ostream & operator<<(std::ostream &os, goo) {return os << "goo";}
};

/******************************************************************************/

auto test1 = unit_test("test-1", COMMENT("This is a test"), [](cpy::Context ct, bool b) {
    ct("a message");
    int n = ct.section("new-section", [](cpy::Context ct) {
        ct.equal(3, 4);
        return 5;
    });
    ct("hmm");
    ct(b);

    std::cerr << "Hey I am std::cerr 1" << std::endl;
    std::cout << "Hey I am std::cout 1" << std::endl;

    ct.timed(1, []{return 1;});

    ct(HERE).near(5, 5.0);

    auto xxx = 5, yyy = 6;

    ct.equal(xxx, yyy, goo(), COMMENT("x should equal y"));

    if (!ct.equal(1, 2)) return;
}, {{false}});

UNIT_TEST("test-2", "This is a test 2") = [](cpy::Context ct) {
    std::cerr << "Hey I am std::cerr 2" << std::endl;
    std::cout << "Hey I am std::cout 2" << std::endl;


    std::cout << sizeof(std::monostate) << " sizeof(std::monostate)" << std::endl;
    std::cout << sizeof(bool)  << " sizeof(bool) " << std::endl;
    std::cout << sizeof(std::any)  << " sizeof(std::any) " << std::endl;
    std::cout << sizeof(cpy::Integer)  << " sizeof(Integer) " << std::endl;
    std::cout << sizeof(cpy::Real)  << " sizeof(Real) " << std::endl;
    std::cout << sizeof(std::complex<cpy::Real>)  << " sizeof(std::complex<Real>) " << std::endl;
    std::cout << sizeof(std::string)  << " sizeof(std::string) " << std::endl;
    std::cout << sizeof(std::string_view) << " sizeof(std::string_view)" << std::endl;
    std::cout << sizeof(cpy::Value) << " sizeof(Value)" << std::endl;

        return 8.9;
    //return "hello";
    // if (!ct.throws_as<std::runtime_error>([]{})) return;
};

UNIT_TEST("test-3") = [](auto ct) {
    std::cout << cpy::get_value("max_time").as_real() << std::endl;
    throw std::runtime_error("runtime_error: uh oh");
};

UNIT_TEST("test-4") = [](auto ct) {
    throw cpy::Skip("this test is skipped");
    ct.equal(5, 5);
};

void each(double) {}

void each(cpy::KeyPair) {}

template <class T=cpy::KeyPair>
void test_var(T t) {each(t);}

template <class T=cpy::KeyPair, class U=cpy::KeyPair>
void test_var(T t, U u) {each(t); each(u);}


template <class ...Ts>
void test_var2(Ts ...ts) {(each(ts), ...);}

template <class T=std::initializer_list<cpy::KeyPair>>
void test_var2(T t) {for (auto i: t) each(std::move(i));}

UNIT_TEST("test-5") = [](auto ct) {
    ct.equal(5, 5);
    test_var(6, 5.5);
    test_var({"hmm", 5.5});
    test_var({"hmm", 5.5}, {"hmm", 5.5});
    test_var2({{"hmm", 5.5}, {"hmm", 5.5}});
};

/******************************************************************************/
