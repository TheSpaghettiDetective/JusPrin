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

#include <functional>
#include <optional>
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
    virtual Snapshot::Onboarding onboarding()                   = 0;

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

    // Print reports (boards 8a to 8d), which need the person's account.
    virtual bool reports_offered() const { return false; }
    // The OS file dialog; nothing when the person cancels.
    virtual std::optional<PrintReportPhoto> choose_report_photo() { return std::nullopt; }
    // Sends the report and writes what the server answered onto the G-code's
    // record; `done` runs later, on the main thread.
    virtual void send_print_report(const PrintReportForm& form, std::function<void(const PrintReportAnswer&)> done) {}

    // First-run onboarding, Welcome → Printers → Home (OnboardingModel.hpp).
    // Called on Home's poll: whether a sign-in, the account's credits, a
    // closed Preferences window or the add-printer form changed what the
    // page shows since the last call.
    virtual bool poll_onboarding() { return false; }
    // The welcome's doors. Signing in opens the browser and the card waits;
    // the page leaves the welcome once it lands.
    virtual void start_sign_in()                  = 0;
    virtual void cancel_sign_in()                 = 0;
    // The email door: the address, then the person picks an emailed code or
    // types the password, and a new code on request. The address alone sends
    // nothing; each of the others waits on the server, and the poll brings
    // its answer.
    virtual void sign_in_with_email(const std::string& email)       = 0;
    virtual void submit_sign_in_code(const std::string& code)       = 0;
    virtual void submit_sign_in_password(const std::string& password) = 0;
    virtual void send_sign_in_code()              = 0;
    // The browser wait's "Open the browser again": the same sign-in's page.
    virtual void reopen_sign_in_browser()         = 0;
    // A page in the browser: "reset_password" on the account's server,
    // "terms" or "privacy".
    virtual void open_sign_in_link(const std::string& link) = 0;
    // Preferences › Agent over the welcome, for "key" or "tool": closed with
    // a verified key or with Done on an AI tool, the welcome is left.
    virtual void open_agent_door(const std::string& door) = 0;
    // "Just slice for now", and the end of a sign-in.
    virtual void leave_welcome()                  = 0;
    // The Printers step. Back returns to the screen before: the OrcaSlicer
    // screen from the add-printer form its Start fresh opened, otherwise
    // the welcome.
    virtual void step_back()                      = 0;
    virtual void start_fresh()                    = 0;
    // "Can't find your printer? Ask the agent": Home, with the printer chat.
    virtual void ask_agent_for_printer()          = 0;
    // Each returns a message for the person when it could not finish.
    // `printers` names the printers the person left chosen, as the
    // OrcaSlicer screen lists them.
    virtual std::string import_orca_printers(const std::vector<std::string>& printers) = 0;
    virtual std::string assume_default_printer()  = 0;
    // The import's result: Continue, which leaves for the add-printer form
    // when no printer came.
    virtual void finish_orca_import()             = 0;
    // The add-printer form's own requests: form_options and form_finish.
    virtual void printer_form_message(const nlohmann::json& payload) = 0;
    // Home's Agent group: the account's credits are shown once.
    virtual void dismiss_account_card()           = 0;

    // The printer card's menu. Each returns an empty string when the action
    // ran or the person cancelled it, and otherwise a message for the person,
    // already translated: the request was understood but cannot be carried
    // out, such as a name another profile has.
    virtual std::string open_printer_settings(const std::string& printer_id)                       = 0;
    // The printer's own chat, to ask about those settings.
    virtual std::string ask_about_printer_settings(const std::string& printer_id)                  = 0;
    virtual std::string connect_printer(const std::string& printer_id) { return "Connection is unavailable."; }
    // A calibration of a filament on this printer, in the project chat.
    virtual std::string calibrate_printer(const std::string& printer_id) { return "Calibration is unavailable."; }
    virtual std::string rename_printer(const std::string& printer_id, const std::string& new_name) = 0;
    virtual std::string remove_printer(const std::string& printer_id)                              = 0;
};

}}}} // namespace Slic3r::GUI::JusPrin::Home
