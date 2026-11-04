#pragma once

#include <iostream>
#include <string>

inline int husk_test_failures = 0;

inline void husk_test_check(
    bool condition,
    const char *expression,
    const char *file,
    int line
) {
    if (!condition) {
        std::cerr << file << ':' << line << ": CHECK failed: " << expression
                  << '\n';
        ++husk_test_failures;
    }
}

template <typename A, typename B>
void husk_test_check_eq(
    const A &actual,
    const B &expected,
    const char *actual_expression,
    const char *expected_expression,
    const char *file,
    int line
) {
    if (!(actual == expected)) {
        std::cerr << file << ':' << line << ": CHECK_EQ failed: "
                  << actual_expression << " == " << expected_expression
                  << "\n  actual:   " << actual << "\n  expected: " << expected
                  << '\n';
        ++husk_test_failures;
    }
}

#define CHECK(condition) \
    husk_test_check((condition), #condition, __FILE__, __LINE__)

#define CHECK_EQ(actual, expected) \
    husk_test_check_eq( \
        (actual), (expected), #actual, #expected, __FILE__, __LINE__ \
    )
