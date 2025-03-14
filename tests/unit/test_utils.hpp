#pragma once

#include <catch2/catch.hpp>
#include <sstream>
#include <string>
#include <vector>
#include <clocale>

namespace TestUtils {

// Helper function to convert formatted string to double
inline double str_to_double(const std::string& str) {
    std::istringstream iss(str);
    double value;
    iss >> value;
    return value;
}

// Helper function to convert vector to string
template<typename T>
std::string vec_to_string(const std::vector<T>& vec) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < vec.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << vec[i];
    }
    oss << "]";
    return oss.str();
}

// Helper function for approximate floating point comparisons
template<typename T>
bool approx_equal(T a, T b, T epsilon = std::numeric_limits<T>::epsilon() * 100) {
    return std::abs(a - b) <= epsilon * std::max(std::abs(a), std::abs(b));
}

// Helper class for temporary locale changes
class ScopedLocale {
    std::string old_locale;
public:
    explicit ScopedLocale(const char* new_locale) {
        old_locale = std::setlocale(LC_ALL, nullptr);
        std::setlocale(LC_ALL, new_locale);
    }
    ~ScopedLocale() {
        std::setlocale(LC_ALL, old_locale.c_str());
    }
};

} // namespace TestUtils