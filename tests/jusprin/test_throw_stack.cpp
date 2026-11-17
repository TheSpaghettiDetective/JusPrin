#include <catch2/catch_test_macros.hpp>

#include "slic3r/GUI/JusPrin/Support/ThrowStack.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Slic3r::GUI::JusPrin;

namespace {

#ifdef _MSC_VER
#define JUSPRIN_NOINLINE __declspec(noinline)
#else
#define JUSPRIN_NOINLINE __attribute__((noinline))
#endif

volatile int g_after_call = 0;

// Calls itself `depth` times, then throws; the work after each call keeps
// every call a frame of its own.
JUSPRIN_NOINLINE void throw_deep(int depth)
{
    if (depth == 0)
        throw std::runtime_error("thrown deep");
    throw_deep(depth - 1);
    g_after_call = g_after_call + 1;
}

// Whether `frame` is a return address inside `function`, whose code is far
// shorter than 1 KiB.
template<class Function> bool inside(Function* function, void* frame)
{
    const auto start   = reinterpret_cast<std::uintptr_t>(function);
    const auto address = reinterpret_cast<std::uintptr_t>(frame);
    return address >= start && address < start + 1024;
}

bool in_throw_deep(void* frame) { return inside(&throw_deep, frame); }

// Finds a failure that is no exception, as code holding an HTTP status does.
JUSPRIN_NOINLINE std::vector<void*> find_failure_as_value()
{
    std::vector<void*> stack = failure_stack();
    g_after_call = g_after_call + 1;
    return stack;
}

} // namespace

TEST_CASE("A report made in a catch block has the stack of the throw", "[ThrowStack]")
{
    start_recording_throws();
    std::vector<void*> stack;
    try {
        throw_deep(3);
    } catch (const std::exception&) {
        stack = current_exception_throw_stack();
    }
    // The throw and the three calls above it.
    CHECK(std::count_if(stack.begin(), stack.end(), in_throw_deep) == 4);
#ifndef _WIN32
    // Innermost first: the throwing function itself.
    REQUIRE_FALSE(stack.empty());
    CHECK(in_throw_deep(stack.front()));
#endif
}

TEST_CASE("Outside a catch block there is no throw stack", "[ThrowStack]")
{
    start_recording_throws();
    CHECK(current_exception_throw_stack().empty());
}

TEST_CASE("A failure that is no exception has the stack of the code that found it", "[ThrowStack]")
{
    start_recording_throws();
    const std::vector<void*> stack = find_failure_as_value();
    REQUIRE_FALSE(stack.empty());
    CHECK(inside(&find_failure_as_value, stack.front()));
}

TEST_CASE("Inside a catch block the failure's stack is the throw's", "[ThrowStack]")
{
    start_recording_throws();
    std::vector<void*> stack;
    try {
        throw_deep(2);
    } catch (const std::exception&) {
        stack = failure_stack();
    }
    CHECK(std::count_if(stack.begin(), stack.end(), in_throw_deep) == 3);
}

TEST_CASE("A throw the app's code does not make never gets an earlier throw's stack", "[ThrowStack]")
{
    start_recording_throws();
    // An exception thrown and caught in the app, and gone: the C++ runtime may
    // hand its memory to the next exception of the same size.
    try {
        throw_deep(0);
    } catch (const std::exception&) {
    }
    // Thrown inside the C++ library itself, where no throw is recorded on
    // macOS: the report has its own stack or none, never throw_deep's.
    std::vector<void*> parse_stack;
    try {
        (void) std::stoi("not a number");
    } catch (const std::invalid_argument&) {
        parse_stack = current_exception_throw_stack();
    }
    CHECK(std::none_of(parse_stack.begin(), parse_stack.end(), in_throw_deep));
    std::vector<void*> index_stack;
    try {
        (void) std::string("abc").at(10);
    } catch (const std::out_of_range&) {
        index_stack = current_exception_throw_stack();
    }
    CHECK(std::none_of(index_stack.begin(), index_stack.end(), in_throw_deep));
}

#ifndef _WIN32
// On Windows std::rethrow_exception raises the exception anew, and the
// handler records that as a throw of its own, at the rethrow.
TEST_CASE("An exception kept and thrown again later still has its throw's stack", "[ThrowStack]")
{
    start_recording_throws();
    std::exception_ptr kept;
    try {
        throw_deep(1);
    } catch (const std::exception&) {
        kept = std::current_exception();
    }
    std::vector<void*> stack;
    try {
        std::rethrow_exception(kept);
    } catch (const std::exception&) {
        stack = current_exception_throw_stack();
    }
    CHECK(std::count_if(stack.begin(), stack.end(), in_throw_deep) == 2);
}
#endif
