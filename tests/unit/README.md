# Unit Tests

This directory contains the unit tests for the project using Google Test framework.

## Directory Structure

```
tests/unit/
├── CMakeLists.txt                         # CMake configuration for unit tests
└── ConfigBase_load_from_json_test.cpp     # Tests for JSON config loading functionality
```

## Building and Running Tests

### Quick Start

Navigate to the project root directory first:
```bash
cd /path/to/project/root  # Replace with actual path to project root
```

Then build the project dependencies:
```bash
./build_release_macos.sh -d  # This generates required dependencies without building the main project
```

Next, create a build directory and run the tests:
```bash
mkdir -p tests/unit/build  # Create build directory inside tests/unit
cd tests/unit/build
cmake ..
cmake --build . --target build_all_tests  # Build all tests
cmake --build . --target run_all_tests    # Run all tests
```

### Individual Test Executables

After building, you can run individual test executables:
```bash
./config_base_load_from_json_test
```

## Writing Tests

Tests use the Google Test framework. Here's a basic example:

```cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>

TEST(TestSuiteName, TestName) {
    EXPECT_EQ(some_function(), expected_value);
    ASSERT_TRUE(some_condition);
}

// For fixture-based tests
class MyTestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code
    }

    void TearDown() override {
        // Cleanup code
    }
};

TEST_F(MyTestFixture, TestName) {
    EXPECT_THAT(some_value, testing::Eq(expected));
}
```

## CMake Targets

- `build_all_tests`: Builds all test executables
- `run_all_tests`: Builds and runs all tests with output on failure
- Individual test targets: `config_base_load_from_json_test`

## Best Practices

1. Use test fixtures for common setup/teardown
2. Use descriptive test names
3. Follow the AAA pattern (Arrange, Act, Assert)
4. Use appropriate assertions (EXPECT_* for non-fatal, ASSERT_* for fatal)
5. Use Google Mock for mocking when needed
6. Keep tests focused and organized

## Adding New Tests

1. Create a new test file in this directory
2. Add the file to `CMakeLists.txt`
3. Include necessary headers (gtest/gtest.h and gmock/gmock.h)
4. Follow the existing test structure and naming conventions
5. Build and run tests to verify everything works