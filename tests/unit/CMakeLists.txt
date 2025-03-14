cmake_minimum_required(VERSION 3.12)
project(Slic3r_tests)

# Enable C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the path to the main project's dependencies
if(NOT DEFINED DEPS_DIR)
    set(DEPS_DIR "${CMAKE_SOURCE_DIR}/../../deps/build_${CMAKE_SYSTEM_PROCESSOR}/OrcaSlicer_dep_${CMAKE_SYSTEM_PROCESSOR}/usr/local")
endif()

# Add the deps directory to CMAKE_PREFIX_PATH
list(APPEND CMAKE_PREFIX_PATH ${DEPS_DIR})

# Find Boost from the main project's deps
find_package(Boost 1.65.0 REQUIRED)

# Find Catch2
find_package(Catch2 3 REQUIRED)

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

# Add test executables
add_executable(locales_utils_test test_locales_utils.cpp)
add_executable(format_test test_format.cpp)

# Link against our libraries
target_link_libraries(locales_utils_test
    PRIVATE
    locales_utils
    Catch2::Catch2
)

target_link_libraries(format_test
    PRIVATE
    Catch2::Catch2
)

target_include_directories(format_test
    PRIVATE
    ${Boost_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/../../src
)

target_link_libraries(format_test
    PRIVATE
    ${Boost_LIBRARIES}
)

# Add tests to CTest
add_test(NAME locales_utils_test COMMAND locales_utils_test)
add_test(NAME format_test COMMAND format_test)

# Add convenience targets
add_custom_target(build_all_tests
    DEPENDS locales_utils_test format_test
    COMMENT "Building all unit tests"
)

add_custom_target(run_all_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS build_all_tests
    COMMENT "Running all unit tests"
)

# Set working directory for tests
set_tests_properties(locales_utils_test format_test
    PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)