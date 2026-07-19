#pragma once

#include <iostream>
#include <string>

namespace test_support {

inline int failures = 0;

inline void check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
        ++failures;
    }
}

inline void checkContains(const std::string& text, const std::string& expected, const char* file,
                          int line) {
    if (text.find(expected) == std::string::npos) {
        std::cerr << file << ':' << line << ": expected text to contain: " << expected << '\n';
        ++failures;
    }
}

} // namespace test_support

#define CHECK(expression)                                                                          \
    ::test_support::check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
#define CHECK_CONTAINS(text, expected)                                                             \
    ::test_support::checkContains((text), (expected), __FILE__, __LINE__)
