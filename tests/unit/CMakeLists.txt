cmake_minimum_required(VERSION 3.12)
project(LocalesUtils_test)

# Enable C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable testing
enable_testing()

# Use the main Catch2 setup
include_directories(${CMAKE_SOURCE_DIR}/../../tests)

# Add fast_float as a header-only library
add_library(fast_float INTERFACE)
target_include_directories(fast_float INTERFACE
    ${CMAKE_SOURCE_DIR}/../../deps/fast_float
)

# Create library for LocalesUtils
add_library(locales_utils
    ${CMAKE_SOURCE_DIR}/../../src/libslic3r/LocalesUtils.cpp
)

target_include_directories(locales_utils
    PUBLIC
    ${CMAKE_SOURCE_DIR}/../../src
)

target_link_libraries(locales_utils
    PUBLIC
    fast_float
)

# Add test executable
add_executable(locales_utils_test test_locales_utils.cpp)

# Link against our library
target_link_libraries(locales_utils_test
    PRIVATE
    locales_utils
)

# Add test to CTest
add_test(NAME locales_utils_test COMMAND locales_utils_test)