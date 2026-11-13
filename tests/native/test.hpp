// Arnés de pruebas mínimo (sin dependencias externas).
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace t {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

inline int& failures() {
    static int f = 0;
    return f;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline int runAll() {
    int failedCases = 0;
    for (const Case& c : registry()) {
        const int before = failures();
        c.fn();
        const bool ok = failures() == before;
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", c.name);
        if (!ok) ++failedCases;
    }
    std::printf("%zu casos, %d fallidos\n", registry().size(), failedCases);
    return failedCases == 0 ? 0 : 1;
}

}  // namespace t

#define T_CAT2(a, b) a##b
#define T_CAT(a, b) T_CAT2(a, b)
#define TEST(name)                                                         \
    static void name();                                                    \
    static ::t::Registrar T_CAT(reg_, name)(#name, &name);                 \
    static void name()

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("  %s:%d: CHECK falló: %s\n", __FILE__, __LINE__, #cond); \
            ++::t::failures();                                                  \
        }                                                                       \
    } while (0)
