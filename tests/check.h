#pragma once

#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace pkmn::test {

struct Case {
    std::string name;
    std::function<void()> fn;
};

std::vector<Case>& registry();
void report_fail(const char* expr, const char* file, int line, const std::string& msg);
int run_all(int argc, char** argv);

struct Registrar {
    Registrar(const char* name, std::function<void()> fn)
    {
        registry().push_back(Case { name, std::move(fn) });
    }
};

}

#define TEST_CASE(test_name)                                                                       \
    static void test_name();                                                                       \
    static ::pkmn::test::Registrar test_registrar_##test_name(#test_name, test_name);              \
    static void test_name()

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            ::pkmn::test::report_fail(#cond, __FILE__, __LINE__, "");                              \
        }                                                                                          \
    } while (0)

#define CHECK_MSG(cond, msg)                                                                       \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            ::pkmn::test::report_fail(#cond, __FILE__, __LINE__, (msg));                           \
        }                                                                                          \
    } while (0)

#define CHECK_THROWS(expr)                                                                         \
    do {                                                                                           \
        bool threw = false;                                                                        \
        try {                                                                                      \
            (void)(expr);                                                                          \
        } catch (...) {                                                                            \
            threw = true;                                                                          \
        }                                                                                          \
        if (!threw) {                                                                              \
            ::pkmn::test::report_fail("throws: " #expr, __FILE__, __LINE__, "");                   \
        }                                                                                          \
    } while (0)
