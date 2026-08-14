#pragma once

class wxWindow;

namespace Slic3r::GUI::JusPrin::Workspace {

class IWorkspace;

// Development-only opaque cover used to test whether the legacy Orca UI can
// remain constructed and mapped while a JusPrin window is the sole main
// application surface visible to the user.
void show_invisible_legacy_ui_spike(wxWindow* legacy_child, IWorkspace& workspace);

} // namespace Slic3r::GUI::JusPrin::Workspace
