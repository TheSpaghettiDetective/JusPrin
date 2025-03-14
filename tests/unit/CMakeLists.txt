cmake_minimum_required(VERSION 3.13)
project(Slic3r_tests)

# Enable C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set path to main project's dependencies
if(NOT DEPS_DIR)
    set(DEPS_DIR ${CMAKE_SOURCE_DIR}/../../deps/build_arm64/OrcaSlicer_dep_arm64/usr/local)
endif()

# Add dependencies directory to CMAKE_PREFIX_PATH
list(APPEND CMAKE_PREFIX_PATH ${DEPS_DIR})

# Find required packages
find_package(OpenSSL REQUIRED)
find_package(TBB REQUIRED)
find_package(Boost REQUIRED COMPONENTS locale log log_setup thread filesystem system regex)
find_package(Catch2 3 REQUIRED)

# Enable testing
enable_testing()
include(CTest)
include(Catch)

# Configure version header
set(SLIC3R_APP_NAME "OrcaSlicer")
set(SLIC3R_APP_KEY "OrcaSlicer")
set(SLIC3R_VERSION "1.0.0")
set(SoftFever_VERSION "1.0.0")
set(SLIC3R_BUILD_ID "1")
string(TIMESTAMP SLIC3R_BUILD_TIME "%Y-%m-%d %H:%M:%S")
set(BBL_RELEASE_TO_PUBLIC 1)
set(BBL_INTERNAL_TESTING 0)
set(ORCA_CHECK_GCODE_PLACEHOLDERS 1)

configure_file(${CMAKE_SOURCE_DIR}/../../src/libslic3r/libslic3r_version.h.in ${CMAKE_BINARY_DIR}/libslic3r_version.h @ONLY)

# Add libraries
add_library(fast_float INTERFACE)
target_include_directories(fast_float INTERFACE ${CMAKE_SOURCE_DIR}/../../src)

# Add semver library
add_library(semver STATIC
    ${CMAKE_SOURCE_DIR}/../../src/semver/semver.c
)
target_include_directories(semver PUBLIC
    ${CMAKE_SOURCE_DIR}/../../src
)

add_library(locales_utils STATIC ${CMAKE_SOURCE_DIR}/../../src/libslic3r/LocalesUtils.cpp)
target_include_directories(locales_utils PUBLIC
    ${CMAKE_SOURCE_DIR}/../../src/libslic3r
    ${CMAKE_SOURCE_DIR}/../../src
    ${CMAKE_BINARY_DIR}
    ${Boost_INCLUDE_DIRS}
)
target_link_libraries(locales_utils PUBLIC
    fast_float
    ${Boost_LIBRARIES}
)

add_library(config STATIC
    ${CMAKE_SOURCE_DIR}/../../src/libslic3r/Config.cpp
    ${CMAKE_SOURCE_DIR}/../../src/libslic3r/utils.cpp
    ${CMAKE_SOURCE_DIR}/../../src/libslic3r/MacUtils.mm
)
target_include_directories(config PUBLIC
    ${CMAKE_SOURCE_DIR}/../../src/libslic3r
    ${CMAKE_SOURCE_DIR}/../../src
    ${CMAKE_SOURCE_DIR}/../../src/eigen
    ${CMAKE_BINARY_DIR}
    ${Boost_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/..
    ${OPENSSL_INCLUDE_DIR}
    ${TBB_INCLUDE_DIRS}
)
target_link_libraries(config PUBLIC
    locales_utils
    fast_float
    semver
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    TBB::tbb
    Boost::locale
    Boost::log
    Boost::log_setup
    Boost::thread
    Boost::filesystem
    Boost::system
    Boost::regex
)

set_source_files_properties(${CMAKE_SOURCE_DIR}/../../src/libslic3r/MacUtils.mm PROPERTIES
    COMPILE_FLAGS "-x objective-c++"
)

# Add test executables
add_executable(locales_utils_test test_locales_utils.cpp)
target_include_directories(locales_utils_test PRIVATE
    ${CMAKE_SOURCE_DIR}/..
)
target_link_libraries(locales_utils_test PRIVATE
    locales_utils
    Catch2::Catch2WithMain
)

add_executable(format_test test_format.cpp)
target_include_directories(format_test PRIVATE
    ${CMAKE_SOURCE_DIR}/..
)
target_link_libraries(format_test PRIVATE
    fast_float
    Catch2::Catch2WithMain
)

add_executable(config_test test_config.cpp)
target_include_directories(config_test PUBLIC
    ${CMAKE_SOURCE_DIR}/../../src
    ${CMAKE_SOURCE_DIR}/../../src/eigen
    ${Boost_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/..
)
target_link_libraries(config_test PRIVATE
    config
    Catch2::Catch2WithMain
)

# Add tests to CTest
add_test(NAME locales_utils_test COMMAND locales_utils_test)
add_test(NAME format_test COMMAND format_test)
add_test(NAME config_test COMMAND config_test)

# Add convenience targets for building and running all tests
add_custom_target(build_all_tests
    DEPENDS locales_utils_test format_test config_test
)

add_custom_target(run_all_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS build_all_tests
)

# Set working directory for tests
set_tests_properties(locales_utils_test format_test config_test
    PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)