#pragma once

// Minimal in-tree test harness — no external dependencies.

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace mgetest {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failureCount() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

inline int runAll() {
    int failedTests = 0;
    for (const auto& test : registry()) {
        const int before = failureCount();
        test.fn();
        if (failureCount() > before) {
            ++failedTests;
            printf("[FAIL] %s\n", test.name);
        } else {
            printf("[ ok ] %s\n", test.name);
        }
    }
    printf("%zu tests, %d failed\n", registry().size(), failedTests);
    return failedTests == 0 ? 0 : 1;
}

}  // namespace mgetest

#define MGE_TEST(name)                                                          \
    static void mge_test_##name();                                              \
    static ::mgetest::Registrar mge_registrar_##name(#name, mge_test_##name);   \
    static void mge_test_##name()

#define MGE_CHECK(cond)                                                         \
    do {                                                                        \
        if (!(cond)) {                                                          \
            printf("  CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++::mgetest::failureCount();                                        \
        }                                                                       \
    } while (0)

#define MGE_CHECK_NEAR(a, b, eps)                                               \
    do {                                                                        \
        const double va = (a), vb = (b);                                        \
        if (std::fabs(va - vb) > (eps)) {                                       \
            printf("  CHECK_NEAR failed at %s:%d: %s=%f vs %s=%f\n", __FILE__,  \
                   __LINE__, #a, va, #b, vb);                                   \
            ++::mgetest::failureCount();                                        \
        }                                                                       \
    } while (0)
