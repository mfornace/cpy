#include <cpytest/Stream.h>
#include <cpy/Document.h>
#include <cpytest/Suite.h>
#include <sstream>

namespace cpy {

// NOTE: this involves double erasure...
struct ValueHandler {
    Caller context;
    Function fun;
    bool operator()(Event e, Scopes const &scopes, Logs &&logs) {
        auto out = fun(context, Integer(e), scopes, std::move(logs));
        if (auto b = out.request<bool>()) return *b;
        std::cout << "not bool " << Variable(out).type().name() << std::endl;
        return true;
    }
};

struct ValueTest {
    Function fun;
    Variable operator()(Context &ct, ArgPack args) const {
        return fun(static_cast<Caller &>(ct), args);
    }
};

/******************************************************************************/

Vector<Variable> run_test(Caller &ct0, std::size_t i, Vector<Function> calls,
                        Variable args, bool cout, bool cerr) {
    auto const test = suite().at(i);
    if (!test.function) throw std::runtime_error("Test case has invalid Function");
    ArgPack pack;
    if (auto p = args.target<Integer const &>())
        pack = test.parameters.at(*p);
    std::stringstream out, err;
    Variable return_value;
    double test_time = 0;
    Vector<Counter> counts(calls.size());
    for (auto &c : counts) c.store(0u);
    {
        RedirectStream o(cout_sync, cout ? out.rdbuf() : nullptr);
        RedirectStream e(cerr_sync, cerr ? err.rdbuf() : nullptr);

        Vector<Handler> handlers;
        for (auto &f : calls) handlers.emplace_back(ValueHandler{ct0, std::move(f)});

        Context ct(ct0, {test.name}, std::move(handlers), &counts);

        auto const start = Clock::now();

        try {return_value = test.function(ct, std::move(pack));}
        catch (ClientError const &) {throw;}
        catch (std::bad_alloc const &) {throw;}
        catch (WrongType const &e) {std::cout << "hmm " << e.what() << e.source.name() << e.dest.name() << std::endl;}
        catch (WrongNumber const &e) {std::cout << "hmm " << e.what() << e.expected << e.received << std::endl;}
        catch (std::exception const &e) {
            std::cout << "error2: " << e.what() << std::endl;
        }
        catch (...) {} // Silence any other exceptions from inside the test

        test_time = std::chrono::duration<double>(Clock::now() - start).count();
    }

    return {std::move(return_value), test_time, mapped<Variable>(counts, [](auto const &i) {
        return i.load(std::memory_order_relaxed);
    }), out.str(), err.str()};
}

/******************************************************************************/

bool make_document() {
    auto &doc = document();
    doc.function("n_tests", [] {
        return suite().size();
    });

    doc.function("compile_info", []() -> Vector<std::string_view> {
        return {__VERSION__ "", __DATE__ "", __TIME__ ""};
    });

    doc.function("test_names", [] {
        return mapped<Variable>(suite(), [](auto &&x) {return x.name;});
    });

    doc.function("test_info", [](std::size_t i) -> Vector<Variable> {
        auto const &c = suite().at(i);
        return {c.name, c.comment.location.file, Integer(c.comment.location.line), c.comment.comment};
    });

    doc.function("n_parameters", [](std::size_t i) {
        return suite().at(i).parameters.size();
    });

    doc.function("add_value", [](std::string_view s, Variable v) {
        add_test(TestCase{std::string(s), {}, ValueAdaptor{std::move(v)}, {}});
    });

    doc.function("run_test", [](Caller ct, std::size_t i, Vector<Function> calls, Variable args, bool cout, bool cerr) {
        return run_test(ct, i, std::move(calls), std::move(args), cout, cerr);
    });

    doc.function("add_test", [](std::string s, Function f, Vector<ArgPack> params) {
        add_test(TestCase{std::move(s), {}, ValueTest{f}, std::move(params)});
    });

    return true;
}

bool blah = make_document();

}