#pragma once

// Where an exception was thrown, so a report's stack is the throw's and not
// the handler's that reported it (AGENTS.md, Error handling: a failure
// travels up unchanged to one handler). Boost 1.84 in this build cannot do
// this (boost::stacktrace::from_current_exception came in 1.85), nor can
// Sentry's SDK, so the app records each C++ throw itself.
//
// macOS and Linux: every throw from the app's own code and its static
// libraries (wx, boost, OCCT...) passes the app's __cxa_throw. A throw inside
// the system's C++ library (libc++.dylib on macOS) is not seen. Linux's
// libstdc++ calls the app's, so it is. A throw is found by its thrown
// object's address, and the C++ runtime gives a freed exception's address to
// the next one, so a record holds its exception from the app's first catch
// of it (ThrowStack.cpp, hold_caught): a throw that was not seen gets no
// stack, not an earlier throw's.
// Windows: a vectored exception handler sees every C++ throw in the process
// and records it on the throwing thread; a report finds the exception it
// handles there by its thrown object.

#include <vector>

namespace Slic3r::GUI::JusPrin {

// Starts recording on Windows; does nothing elsewhere, where recording needs
// no start. Once, early.
void start_recording_throws();

// The stack, innermost first, where the exception being handled was thrown;
// empty outside a catch block or when its throw was not recorded.
std::vector<void*> current_exception_throw_stack();

// Where the failure being reported was found, innermost first: inside a
// catch block, the stack of its throw; otherwise, as for a failure that
// arrived as a value (an HTTP status), the caller's own stack. Taken before
// a report moves to the main thread, which would lose both.
std::vector<void*> failure_stack();

} // namespace Slic3r::GUI::JusPrin
