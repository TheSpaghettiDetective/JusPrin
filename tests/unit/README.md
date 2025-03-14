# Unit Tests

This directory contains the unit tests for the project using the Catch2 testing framework.

## Directory Structure

```
tests/unit/
├── CMakeLists.txt          # CMake configuration for unit tests
├── test_format.cpp         # Tests for format functionality
├── test_locales_utils.cpp  # Tests for locale utilities
└── test_utils.hpp         # Shared test utilities
```

## Building and Running Tests

### Quick Start

First, build the project dependencies:
```bash
./build_release_macos.sh -d  # This generates required dependencies without building the main project
```

Then from the project root:
```bash
mkdir -p build_unit_tests
cd build_unit_tests
cmake ../tests/unit
cmake --build . --target build_all_tests  # Build all tests
cmake --build . --target run_all_tests    # Run all tests
```

### Individual Test Executables

After building, you can run individual test executables:
```bash
./format_test
./locales_utils_test
```

## Test Utilities

The `test_utils.hpp` header provides several utilities to make testing easier:

### String to Double Conversion
```cpp
double result = TestUtils::str_to_double("3.14159");
```

### Vector Formatting
```cpp
std::vector<int> vec{1, 2, 3};
std::string result = TestUtils::vec_to_string(vec); // "[1, 2, 3]"
```

### Floating Point Comparisons
```cpp
bool equal = TestUtils::approx_equal(3.14159, 3.14160, 0.0001);
```

### Locale Management
```cpp
{
    TestUtils::ScopedLocale locale("en_US.UTF-8");
    // Code that needs specific locale
} // Locale automatically restored
```

## Writing Tests

Tests use the Catch2 framework. Here's a basic example:

```cpp
#include <catch2/catch.hpp>
#include "test_utils.hpp"

TEST_CASE("My Test Case", "[tag]") {
    SECTION("Test section") {
        REQUIRE(some_function() == expected_value);
    }
}
```

## CMake Targets

- `build_all_tests`: Builds all test executables
- `run_all_tests`: Builds and runs all tests with output on failure
- Individual test targets: `format_test`, `locales_utils_test`

## Best Practices

1. Use the provided test utilities for common operations
2. Group related tests in sections
3. Use meaningful tags for test cases
4. Add descriptive test names and failure messages
5. Keep test files focused and organized
6. Use the ScopedLocale when testing locale-dependent functionality

## Adding New Tests

1. Create a new test file in this directory
2. Add the file to `CMakeLists.txt`
3. Include necessary headers and test utilities
4. Follow the existing test structure and naming conventions
5. Build and run tests to verify everything works