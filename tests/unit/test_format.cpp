#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include "libslic3r/format.hpp"
#include "test_utils.hpp"
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <sstream>

using namespace Slic3r;
using namespace TestUtils;

// Custom type to test format with user-defined types
class CustomType {
    int value;
public:
    CustomType(int v) : value(v) {}
    friend std::ostream& operator<<(std::ostream& os, const CustomType& ct) {
        os << "CustomType(" << ct.value << ")";
        return os;
    }
};

// Helper function to convert formatted string to double
double str_to_double(const std::string& str) {
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

// Stream operator for vectors
template<typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& vec) {
    os << "[";
    for (size_t i = 0; i < vec.size(); ++i) {
        if (i > 0) os << ", ";
        os << vec[i];
    }
    os << "]";
    return os;
}

// Test basic formatting with different argument types
TEST_CASE("Basic Format Tests", "[format]") {
    SECTION("Empty format string") {
        REQUIRE(format("") == "");
        REQUIRE(format(std::string("")) == "");
    }

    SECTION("String without placeholders") {
        REQUIRE(format("Hello World") == "Hello World");
        REQUIRE(format(std::string("Hello World")) == "Hello World");
    }

    SECTION("Single argument formatting") {
        REQUIRE(format("Number: %1%", 42) == "Number: 42");
        REQUIRE(format("String: %1%", "test") == "String: test");
        double formatted_value = str_to_double(format("%1%", 3.14159));
        REQUIRE(formatted_value == Catch::Detail::Approx(3.14159).epsilon(0.00001));
        REQUIRE(format("Bool: %1%", true) == "Bool: 1");
    }

    SECTION("Multiple argument formatting") {
        REQUIRE(format("%1% + %2% = %3%", 1, 2, 3) == "1 + 2 = 3");
        REQUIRE(format("%1%, %2%, %3%", "a", "b", "c") == "a, b, c");
    }
}

// Test format with different numeric types
TEST_CASE("Numeric Format Tests", "[format]") {
    SECTION("Integer types") {
        REQUIRE(format("%1%", (short)42) == "42");
        REQUIRE(format("%1%", (int)42) == "42");
        REQUIRE(format("%1%", (long)42) == "42");
        REQUIRE(format("%1%", (long long)42) == "42");
        REQUIRE(format("%1%", (unsigned)42) == "42");
    }

    SECTION("Floating point types") {
        float float_val = str_to_double(format("%1%", (float)3.14));
        REQUIRE(float_val == Catch::Detail::Approx(3.14f).epsilon(0.00001));
        double double_val = str_to_double(format("%1%", (double)3.14159));
        REQUIRE(double_val == Catch::Detail::Approx(3.14159).epsilon(0.00001));
        long double ldouble_val = str_to_double(format("%1%", (long double)3.14159));
        REQUIRE(ldouble_val == Catch::Detail::Approx(3.14159L).epsilon(0.00001));
    }

    SECTION("Special numeric values") {
        REQUIRE(format("%1%", std::numeric_limits<double>::infinity()) == "inf");
        REQUIRE(format("%1%", -std::numeric_limits<double>::infinity()) == "-inf");
        REQUIRE(format("%1%", std::numeric_limits<double>::quiet_NaN()).find("nan") != std::string::npos);
    }
}

// Test format with different string types
TEST_CASE("String Format Tests", "[format]") {
    SECTION("String literals and std::string") {
        REQUIRE(format("%1%", "literal") == "literal");
        REQUIRE(format("%1%", std::string("string")) == "string");
        REQUIRE(format("%1%", std::string_view("view")) == "view");
    }

    SECTION("Empty strings") {
        REQUIRE(format("%1%", "") == "");
        REQUIRE(format("%1%", std::string()) == "");
    }

    SECTION("Strings with special characters") {
        REQUIRE(format("%1%", "Hello\nWorld") == "Hello\nWorld");
        REQUIRE(format("%1%", "Tab\there") == "Tab\there");
        REQUIRE(format("%1%", R"(Raw\string)") == R"(Raw\string)");
    }
}

// Test format with custom types
TEST_CASE("Custom Type Format Tests", "[format]") {
    SECTION("Custom type with operator<<") {
        CustomType ct(42);
        REQUIRE(format("%1%", ct) == "CustomType(42)");
    }

    SECTION("Smart pointers") {
        auto ptr = std::make_shared<int>(42);
        REQUIRE(format("%1%", *ptr) == "42");
    }
}

// Test format with containers
TEST_CASE("Container Format Tests", "[format]") {
    SECTION("Vector formatting") {
        std::vector<int> vec{1, 2, 3};
        REQUIRE(format("%1%", vec_to_string(vec)) == "[1, 2, 3]");
    }

    SECTION("Empty container formatting") {
        std::vector<int> empty_vec;
        REQUIRE_NOTHROW(format("%1%", vec_to_string(empty_vec)));
    }
}

// Test error handling
TEST_CASE("Error Handling Tests", "[format]") {
    SECTION("Too few arguments") {
        REQUIRE_THROWS_AS(format("%1% %2%", 1), boost::io::too_few_args);
    }

    SECTION("Too many arguments") {
        REQUIRE_THROWS_AS(format("%1%", 1, 2), boost::io::too_many_args);
    }

    SECTION("Invalid format string") {
        REQUIRE_THROWS_AS(format("%1", 1), boost::io::bad_format_string);
    }
}

// Test position-independent formatting
TEST_CASE("Position Independent Format Tests", "[format]") {
    SECTION("Reordered arguments") {
        REQUIRE(format("%2% %1%", "second", "first") == "first second");
        REQUIRE(format("%3% %1% %2%", "three", "one", "two") == "two three one");
    }

    SECTION("Repeated arguments") {
        REQUIRE(format("%1% %1% %1%", "repeat") == "repeat repeat repeat");
    }
}

// Test format with mixed types
TEST_CASE("Mixed Type Format Tests", "[format]") {
    SECTION("Mixed numeric and string") {
        REQUIRE(format("%1% %2% %3%", 42, "test", 3.14) == "42 test 3.14");
    }

    SECTION("Mixed custom and standard types") {
        CustomType ct(42);
        REQUIRE(format("%1% %2% %3%", ct, "test", 3.14) == "CustomType(42) test 3.14");
    }
}

// Test thread safety
TEST_CASE("Thread Safety Tests", "[format]") {
    SECTION("Concurrent formatting") {
        const int num_threads = 4;
        std::vector<std::thread> threads;
        std::vector<std::string> results(num_threads);

        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([i, &results]() {
                results[i] = format("Thread %1%", i);
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        for (int i = 0; i < num_threads; ++i) {
            REQUIRE(results[i] == format("Thread %1%", i));
        }
    }
}