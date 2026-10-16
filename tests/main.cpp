#include "check.h"

namespace pkmn::test {

namespace {
    int g_failures = 0;
}

std::vector<Case>& registry()
{
    static std::vector<Case> cases;
    return cases;
}

void report_fail(const char* expr, const char* file, int line, const std::string& msg)
{
    ++g_failures;
    std::fprintf(
        stderr, "  FAIL %s:%d: %s%s%s\n", file, line, expr, msg.empty() ? "" : " :: ", msg.c_str());
}

int run_all(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (Case& c : registry()) {
        if (filter != nullptr && c.name.find(filter) == std::string::npos) {
            continue;
        }
        ++ran;
        int before = g_failures;
        std::printf("[ RUN  ] %s\n", c.name.c_str());
        try {
            c.fn();
        } catch (const std::exception& e) {
            report_fail("<exception>", __FILE__, __LINE__, e.what());
        } catch (...) {
            report_fail("<exception>", __FILE__, __LINE__, "non-standard exception");
        }
        if (g_failures == before) {
            std::printf("[ PASS ] %s\n", c.name.c_str());
        } else {
            std::printf("[ FAIL ] %s\n", c.name.c_str());
        }
    }
    std::printf("\n%d test(s) run, %d failure(s)\n", ran, g_failures);
    return g_failures == 0 ? 0 : 1;
}

}

int main(int argc, char** argv) { return pkmn::test::run_all(argc, argv); }
