#pragma once

namespace Slic3r::GUI {
class Plater;
} // namespace Slic3r::GUI

namespace Slic3r::GUI::JusPrin::Workspace {

class IWorkspace;

// Development-only self test. Drives IWorkspace through a scripted scenario
// against the live application and terminates the process with 0 when every
// assertion held, non-zero otherwise.
//
// Gated on JUSPRIN_WORKSPACE_SELFTEST=1. Optional environment overrides:
//   JUSPRIN_WORKSPACE_SELFTEST_LOG      file to append the transcript to
//   JUSPRIN_WORKSPACE_SELFTEST_FIXTURE  model file to load instead of the
//                                       committed resources/jusprin/selftest fixture
bool workspace_selftest_requested();
void run_workspace_selftest(Plater& plater, IWorkspace& workspace);

} // namespace Slic3r::GUI::JusPrin::Workspace
