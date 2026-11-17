#pragma once

// Once per run, JusPrin tells its server that it started: its version, what
// the system calls itself, and the install id from LaunchRecord. Nothing
// about the person goes with it, and no token. The server counts installs
// and starts from these reports, so it can see how many downloads become a
// JusPrin that runs. A server that answers with a fault is reported, when
// the person has automatic reports on; one that cannot be reached is not.

namespace Slic3r::GUI::JusPrin::Updates {

// Sends the report on first call; later calls in the same run do nothing.
// Stealth mode, which turns the app's own calls off, turns this off too.
void report_launch();

} // namespace Slic3r::GUI::JusPrin::Updates
