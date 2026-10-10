// A very small test harness. Each TEST registers itself before main runs,
// CHECK records a failure and carries on, and the run exits non-zero if any
// check failed.
#pragma once

#include <stdio.h>

void test_register(const char *name, void (*fn)(void));
void test_fail(const char *file, int line, const char *expr);

#define TEST(name)                                                              \
    static void name(void);                                                     \
    __attribute__((constructor)) static void register_##name(void) { test_register(#name, name); } \
    static void name(void)

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) test_fail(__FILE__, __LINE__, #cond);                      \
    } while (0)

#define CHECK_EQ(a, b)                                                          \
    do {                                                                        \
        long long _a = (long long)(a), _b = (long long)(b);                     \
        if (_a != _b) {                                                         \
            char _m[256];                                                       \
            snprintf(_m, sizeof _m, "%s == %s (%lld != %lld)", #a, #b, _a, _b); \
            test_fail(__FILE__, __LINE__, _m);                                  \
        }                                                                       \
    } while (0)
