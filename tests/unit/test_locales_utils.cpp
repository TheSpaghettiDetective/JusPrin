#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include "libslic3r/LocalesUtils.hpp"
#include "test_utils.hpp"
#include <clocale>
#include <thread>
#include <vector>

using namespace Slic3r;
using namespace TestUtils;

TEST_CASE("CNumericLocalesSetter Tests", "[LocalesUtils]") {
    SECTION("Basic Functionality") {
        // Store original locale
        std::string original_locale = setlocale(LC_NUMERIC, nullptr);

        {
            CNumericLocalesSetter setter;
            // Verify decimal point is now '.'
            REQUIRE(is_decimal_separator_point());

            // Test some float formatting
            char buffer[32];
            sprintf(buffer, "%.1f", 1.5);
            REQUIRE(std::string(buffer) == "1.5");
        }

        // Verify locale is restored
        std::string restored_locale = setlocale(LC_NUMERIC, nullptr);
        REQUIRE(restored_locale == original_locale);
    }

    SECTION("Thread Safety") {
        const int NUM_THREADS = 4;
        std::vector<std::thread> threads;
        std::vector<bool> results(NUM_THREADS, false);

        for (int i = 0; i < NUM_THREADS; i++) {
            threads.emplace_back([&results, i]() {
                try {
                    CNumericLocalesSetter setter;
                    // Perform some locale-dependent operations
                    results[i] = is_decimal_separator_point();
                    string_to_double_decimal_point("123.456");
                    float_to_string_decimal_point(123.456);
                } catch (...) {
                    results[i] = false;
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        for (bool result : results) {
            REQUIRE(result);
        }
    }
}

TEST_CASE("string_to_double_decimal_point Tests", "[LocalesUtils]") {
    CNumericLocalesSetter setter;  // Ensure consistent locale for tests

    SECTION("Valid Conversions") {
        size_t pos;

        // Test positive numbers
        REQUIRE(string_to_double_decimal_point("123.456", &pos) == Approx(123.456));
        REQUIRE(pos == 7);

        // Test negative numbers
        REQUIRE(string_to_double_decimal_point("-123.456", &pos) == Approx(-123.456));
        REQUIRE(pos == 8);

        // Test scientific notation
        REQUIRE(string_to_double_decimal_point("1.23e-4", &pos) == Approx(0.000123));
        REQUIRE(string_to_double_decimal_point("1.23E+4", &pos) == Approx(12300.0));

        // Test integers
        REQUIRE(string_to_double_decimal_point("42", &pos) == Approx(42.0));

        // Test zero
        REQUIRE(string_to_double_decimal_point("0.0", &pos) == Approx(0.0));
        REQUIRE(string_to_double_decimal_point("-0.0", &pos) == Approx(0.0));
    }

    SECTION("Edge Cases") {
        size_t pos;

        // Test empty string
        REQUIRE(std::isnan(string_to_double_decimal_point("", &pos)));

        // Test whitespace handling
        REQUIRE(string_to_double_decimal_point("  123.456", &pos) == Approx(123.456));

        // Test very large and small numbers
        REQUIRE(string_to_double_decimal_point("1e308", &pos) == Approx(1e308));
        REQUIRE(string_to_double_decimal_point("1e-308", &pos) == Approx(1e-308));
    }
}

TEST_CASE("float_to_string_decimal_point Tests", "[LocalesUtils]") {
    CNumericLocalesSetter setter;  // Ensure consistent locale for tests

    SECTION("Default Precision") {
        REQUIRE(float_to_string_decimal_point(123.456) == "123.456");
        REQUIRE(float_to_string_decimal_point(-123.456) == "-123.456");
        REQUIRE(float_to_string_decimal_point(0.0) == "0");
        REQUIRE(float_to_string_decimal_point(-0.0) == "0");
    }

    SECTION("Custom Precision") {
        REQUIRE(float_to_string_decimal_point(123.456, 2) == "123.46");
        REQUIRE(float_to_string_decimal_point(123.456, 0) == "123");
        REQUIRE(float_to_string_decimal_point(123.456, 4) == "123.4560");
    }

    SECTION("Edge Cases") {
        // Very large numbers
        REQUIRE_NOTHROW(float_to_string_decimal_point(1e308));

        // Very small numbers
        REQUIRE_NOTHROW(float_to_string_decimal_point(1e-308));

        // Zero with different precisions
        REQUIRE(float_to_string_decimal_point(0.0, 2) == "0.00");
        REQUIRE(float_to_string_decimal_point(-0.0, 2) == "0.00");
    }
}

TEST_CASE("is_decimal_separator_point Tests", "[LocalesUtils]") {
    SECTION("Basic Functionality") {
        CNumericLocalesSetter setter;
        REQUIRE(is_decimal_separator_point());
    }

    SECTION("Locale Change") {
        {
            ScopedLocale de_locale("de_DE.UTF-8");
            REQUIRE_FALSE(is_decimal_separator_point());
        }

        {
            // Verify CNumericLocalesSetter fixes it
            CNumericLocalesSetter setter;
            REQUIRE(is_decimal_separator_point());
        }
    }
}