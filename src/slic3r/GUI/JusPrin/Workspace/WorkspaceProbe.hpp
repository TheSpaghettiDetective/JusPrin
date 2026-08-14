#pragma once

class wxWindow;

namespace Slic3r::GUI::JusPrin::Workspace {

class IWorkspace;

// Development-only probe. The implementation consumes only IWorkspace and
// deliberately has no access to Plater, GLCanvas3D, or Model.
wxWindow* show_workspace_probe(wxWindow* parent, IWorkspace& workspace);

} // namespace Slic3r::GUI::JusPrin::Workspace
