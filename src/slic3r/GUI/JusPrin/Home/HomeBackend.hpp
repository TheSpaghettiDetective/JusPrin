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

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

// One file dropped on the page: its name as the page saw it, and its bytes.
struct DroppedFile
{
    std::string name;
    std::string bytes;
};

class IHomeBackend
{
public:
    virtual ~IHomeBackend() = default;

    // Reads. Called whenever Home is shown or asks for state.
    virtual bool                      dark() const              = 0;
    virtual ProjectList               recent_projects() const   = 0;
    virtual std::vector<PrinterEntry> printers() const          = 0;

    // Actions. Each is the whole product gesture, not a step of one, so the
    // adapter can use whichever upstream entry point already performs it.
    virtual void open_project(const std::string& project_id)    = 0;
    // A blank project, for a print built up part by part.
    virtual void new_project()                                  = 0;
    // A file from the OS file dialog, opened as a new project the way a file
    // opened from Finder or Explorer is; cancelling leaves Home as it is.
    virtual void open_file()                                    = 0;
    // Files dropped on the page, which hands over their names and bytes but
    // not where they came from; opened as open_file opens one.
    virtual void open_dropped_files(const std::vector<DroppedFile>& files) = 0;
    virtual void launch_monitor(const std::string& printer_id)  = 0;
    virtual void add_printer()                                  = 0;
    // JusPrin's Preferences window, over Home.
    virtual void open_preferences() {}

    // The Print history screen. Its entries, every project's, with each
    // exported file checked against what was written (PrintHistory.hpp).
    virtual nlohmann::json print_history() { return nlohmann::json::array(); }
    // Back from a Print history the project opened: to the project's screen.
    virtual void leave_print_history() {}
    // The project's chat at the marker where the G-code went out.
    virtual void open_in_chat(const std::string& project_id, const std::string& conversation_id,
                              const std::string& marker_id) {}
    // The project's kept copy of a G-code, written where the person picks,
    // or, with `beside`, next to that earlier export under a name no file has.
    virtual void export_gcode(const std::string& project_id, const std::string& hash, const std::string& beside) {}
    // OrcaSlicer's send dialog for the kept file of a recorded G-code.
    virtual void print_again(const std::string& project_id, const std::string& hash) {}

    // The printer card's menu. Each returns an empty string when the action
    // ran or the person cancelled it, and otherwise a message for the person,
    // already translated: the request was understood but cannot be carried
    // out, such as a name another profile has.
    virtual std::string open_printer_settings(const std::string& printer_id)                       = 0;
    virtual std::string connect_printer(const std::string& printer_id) { return "Connection is unavailable."; }
    // A calibration of a filament on this printer, in the project chat.
    virtual std::string calibrate_printer(const std::string& printer_id) { return "Calibration is unavailable."; }
    virtual std::string rename_printer(const std::string& printer_id, const std::string& new_name) = 0;
    virtual std::string remove_printer(const std::string& printer_id)                              = 0;
};

}}}} // namespace Slic3r::GUI::JusPrin::Home
