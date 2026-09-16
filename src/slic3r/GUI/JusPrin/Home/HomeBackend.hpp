#pragma once

// What Home needs from the application, in the fork's own vocabulary.
//
// The Agent side of the shell reaches OrcaSlicer through Workspace::IWorkspace
// with OrcaWorkspaceAdapter behind it; this is the same seam for Home. Orca
// types stop at the adapter: HomeHost sees only the structs in
// HomeSnapshot.hpp, which is what lets its protocol handling be tested without
// a GUI, and what keeps the churn in MainFrame and the device layer to one
// file instead of spread through the host.

#include "HomeSnapshot.hpp"

#include <string>
#include <vector>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

class IHomeBackend
{
public:
    virtual ~IHomeBackend() = default;

    // Reads. Called whenever Home is shown or asks for state.
    virtual bool                      dark() const              = 0;
    virtual std::vector<ProjectEntry> recent_projects() const   = 0;
    virtual std::vector<PrinterEntry> printers() const          = 0;

    // Actions. Each is the whole product gesture, not a step of one, so the
    // adapter can use whichever upstream entry point already performs it.
    virtual void open_project(const std::string& project_id)    = 0;
    virtual void new_project()                                  = 0;
    virtual void import_model()                                 = 0;
    virtual void launch_monitor(const std::string& printer_id)  = 0;
    virtual void add_printer()                                  = 0;

    // The printer card's menu. Each returns an empty string when the action
    // ran or the person cancelled it, and otherwise a message for the person,
    // already translated: the request was understood but cannot be carried
    // out, such as a name another profile has.
    virtual std::string open_printer_settings(const std::string& printer_id)                       = 0;
    virtual std::string rename_printer(const std::string& printer_id, const std::string& new_name) = 0;
    virtual std::string remove_printer(const std::string& printer_id)                              = 0;
};

}}}} // namespace Slic3r::GUI::JusPrin::Home
