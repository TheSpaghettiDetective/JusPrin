#include "ThrowStack.hpp"

#include <array>
#include <cstddef>
#include <exception>
#include <mutex>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <cstring>
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <typeinfo>
#endif

namespace Slic3r::GUI::JusPrin {

namespace {

constexpr int kFrames = 64;

struct Throw
{
    const void*                 object = nullptr; // the thrown object, which names the throw
    int                         depth  = 0;
    std::array<void*, kFrames>  frames{};
#ifndef _WIN32
    // The exception itself, once the app's own code has caught it. Holding it
    // keeps its memory, so no other exception gets its address while this
    // record names it.
    std::exception_ptr          held;
#endif
};

#ifndef _WIN32

// The latest throws of every thread: an exception can be thrown on one and
// reported on another (std::exception_ptr). A record names its exception
// only while it holds it: the C++ runtime gives a freed exception's memory to
// the next one, which may be thrown where no throw is recorded (inside
// libc++.dylib on macOS). Never destroyed, so no held exception is destroyed
// as the app exits.
constexpr std::size_t kKept = 32;
struct Throws
{
    std::mutex               mutex;
    std::array<Throw, kKept> kept;
    std::size_t              next = 0;
};
Throws& throws()
{
    static Throws* const all = new Throws();
    return *all;
}

// The object `current` holds: libc++'s and libstdc++'s exception_ptr is the
// thrown object's address.
const void* object_of(const std::exception_ptr& current)
{
    static_assert(sizeof(std::exception_ptr) == sizeof(void*));
    const void* object = nullptr;
    std::memcpy(&object, &current, sizeof object);
    return object;
}

// The newest record of `object`, or none.
Throw* newest_of(Throws& all, const void* object)
{
    for (std::size_t back = 1; back <= kKept && back <= all.next; ++back) {
        Throw& seen = all.kept[(all.next - back) % kKept];
        if (seen.object == object)
            return &seen;
    }
    return nullptr;
}

// Never inlined, so the stack always starts with this function's frame and
// __cxa_throw's, which it leaves out: the first kept is the throwing function.
constexpr int kOwnFrames = 2;
__attribute__((noinline)) void record(const void* object)
{
    Throw throw_here;
    throw_here.object = object;
    std::array<void*, kFrames + kOwnFrames> frames{};
    const int depth = backtrace(frames.data(), static_cast<int>(frames.size()));
    for (int i = kOwnFrames; i < depth; ++i)
        throw_here.frames[static_cast<std::size_t>(i - kOwnFrames)] = frames[static_cast<std::size_t>(i)];
    throw_here.depth = depth > kOwnFrames ? depth - kOwnFrames : 0;
    // Let go of the oldest record's exception after unlocking: destroying it
    // runs its destructor.
    std::exception_ptr oldest;
    Throws&            all = throws();
    std::lock_guard<std::mutex> lock(all.mutex);
    Throw& slot = all.kept[all.next++ % kKept];
    oldest      = std::move(slot.held);
    slot        = std::move(throw_here);
}

// Called as the app's own code catches an exception: the newest record of its
// address takes hold of it, the first time. One case still names the wrong
// throw: a recorded exception that only code outside the app caught and freed,
// then one thrown where nothing is recorded, at the same address, caught by
// the app.
void hold_caught()
{
    std::exception_ptr current = std::current_exception();
    if (!current)
        return;
    Throws&                     all = throws();
    std::lock_guard<std::mutex> lock(all.mutex);
    Throw* seen = newest_of(all, object_of(current));
    if (seen != nullptr && !seen->held)
        seen->held = std::move(current);
}

#else

// The C++ runtime's record of the exception being handled on this thread
// (vcruntime; Boost.Stacktrace reads it the same way).
extern "C" void** __cdecl __current_exception();

constexpr DWORD kCppException = 0xE06D7363;

// The thrown object of the C++ exception being handled on this thread, which
// names its throw; none outside a catch block.
const void* current_object()
{
    const auto* record = static_cast<const EXCEPTION_RECORD*>(*__current_exception());
    if (record == nullptr || record->ExceptionCode != kCppException || record->NumberParameters < 3)
        return nullptr;
    return reinterpret_cast<const void*>(record->ExceptionInformation[1]);
}

// Per thread: the handler runs on the throwing thread, and it sees every
// throw, the C++ library's too.
constexpr std::size_t kKept = 32;
struct Throws
{
    std::array<Throw, kKept> kept;
    std::size_t              next = 0;
};
thread_local Throws t_throws;

LONG CALLBACK on_exception(EXCEPTION_POINTERS* info)
{
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    if (record->ExceptionCode != kCppException || record->NumberParameters < 3)
        return EXCEPTION_CONTINUE_SEARCH;
    const void* object = reinterpret_cast<const void*>(record->ExceptionInformation[1]);
    // A bare `throw;` passes no object, and one that throws the exception
    // being handled again names it: neither is a throw of its own.
    if (object == nullptr || object == current_object())
        return EXCEPTION_CONTINUE_SEARCH;
    Throw& slot = t_throws.kept[t_throws.next++ % kKept];
    slot.object = object;
    slot.depth  = CaptureStackBackTrace(0, kFrames, slot.frames.data(), nullptr);
    return EXCEPTION_CONTINUE_SEARCH;
}

#endif

} // namespace

#ifndef _WIN32

void start_recording_throws() {}

std::vector<void*> current_exception_throw_stack()
{
    const std::exception_ptr current = std::current_exception();
    if (!current)
        return {};
    Throws&                     all = throws();
    std::lock_guard<std::mutex> lock(all.mutex);
    const Throw* seen = newest_of(all, object_of(current));
    if (seen == nullptr || seen->held != current)
        return {};
    return {seen->frames.begin(), seen->frames.begin() + seen->depth};
}

__attribute__((noinline)) std::vector<void*> failure_stack()
{
    std::vector<void*> stack = current_exception_throw_stack();
    if (!stack.empty())
        return stack;
    // Leaves out this function's own frame: the first is its caller.
    std::array<void*, kFrames + 1> frames{};
    const int depth = backtrace(frames.data(), static_cast<int>(frames.size()));
    return depth > 1 ? std::vector<void*>(frames.begin() + 1, frames.begin() + depth) : std::vector<void*>{};
}

#else

void start_recording_throws()
{
    static const PVOID handler = AddVectoredExceptionHandler(1, on_exception);
    (void) handler;
}

std::vector<void*> current_exception_throw_stack()
{
    const void* object = current_object();
    if (object == nullptr)
        return {};
    for (std::size_t back = 1; back <= kKept && back <= t_throws.next; ++back) {
        const Throw& seen = t_throws.kept[(t_throws.next - back) % kKept];
        if (seen.object == object)
            return {seen.frames.begin(), seen.frames.begin() + seen.depth};
    }
    return {};
}

__declspec(noinline) std::vector<void*> failure_stack()
{
    std::vector<void*> stack = current_exception_throw_stack();
    if (!stack.empty())
        return stack;
    // Leaves out this function's own frame: the first is its caller.
    std::array<void*, kFrames> frames{};
    const USHORT depth = CaptureStackBackTrace(1, kFrames, frames.data(), nullptr);
    return {frames.begin(), frames.begin() + depth};
}

#endif

} // namespace Slic3r::GUI::JusPrin

#ifndef _WIN32

// The thrown type's parameter as the compiler already declares it: GCC
// declares __cxa_throw itself, with void*, once it has compiled any throw,
// and refuses a definition that differs; clang has it from <cxxabi.h>.
#if defined(__GNUC__) && !defined(__clang__)
using ThrownType = void;
#else
using ThrownType = std::type_info;
#endif

// Every C++ throw in the app's own code and static libraries comes here
// first (ThrowStack.hpp), then goes on to the C++ library's own.
extern "C" void __cxa_throw(void* object, ThrownType* type, void (*destructor)(void*))
{
    using Real       = void (*)(void*, ThrownType*, void (*)(void*));
    static Real real = reinterpret_cast<Real>(dlsym(RTLD_NEXT, "__cxa_throw"));
    Slic3r::GUI::JusPrin::record(object);
    real(object, type, destructor);
    __builtin_unreachable();
}

// Every catch in the app's own code and static libraries comes here (see
// hold_caught).
extern "C" void* __cxa_begin_catch(void* exception) noexcept
{
    using Real       = void* (*) (void*);
    static Real real = reinterpret_cast<Real>(dlsym(RTLD_NEXT, "__cxa_begin_catch"));
    void* const caught = real(exception);
    Slic3r::GUI::JusPrin::hold_caught();
    return caught;
}

#endif
