#pragma once

// The one place Home meets OrcaSlicer. Everything the gallery and the printer
// rail know is read here and handed on as the fork's own structs; every action
// Home offers is performed here through the upstream entry point that already
// performs it.
//
// Keeping this in one file is the point: MainFrame and the device layer are
// where upstream is actively moving code, so the fork re-derives this file at
// a rebase and nothing else.

#include "HomeBackend.hpp"
#include "MonitorWindow.hpp"
#include "OrcaImporter.hpp"
#include "PrintHistory.hpp"
#include "PrinterWindow.hpp"
#include "slic3r/GUI/JusPrin/Agent/CloudAccount.hpp"

#include <wx/arrstr.h>
#include <wx/weakref.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {
class MainFrame;
}} // namespace Slic3r::GUI
namespace Slic3r::GUI::JusPrin::Workspace {
class ProjectAutosave;
}
namespace Slic3r::GUI::JusPrin::Agent {
class IChatModelSource;
}
namespace Slic3r::GUI::JusPrin::PrinterSetup {
class PrinterCatalog;
}

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

// Where a file dropped on Home is kept: a temporary copy, since the page
// gives the file's bytes and not its path.
std::filesystem::path dropped_files_folder();

class OrcaHomeBackend final : public IHomeBackend
{
public:
    explicit OrcaHomeBackend(MainFrame& frame);
    ~OrcaHomeBackend() override;

    // How Home opens the printer conversation, which replaces its printers
    // column: an empty name adds a printer, a name changes that one. The
    // shell owns the panel; Home only says what the person asked about.
    using OpenConversation = std::function<void(const std::string& printer_name, bool connect)>;
    void set_conversation_opener(OpenConversation open) { m_open_conversation = std::move(open); }
    // How Home starts a calibration on a printer: the shell asks first, then
    // hands it to the project chat.
    using StartCalibration = std::function<void(const std::string& printer_name, const std::vector<std::string>& spools)>;
    void set_calibration_starter(StartCalibration start) { m_start_calibration = std::move(start); }
    // Also settles, once, whether someone who used JusPrin before is welcomed.
    void set_autosave(Workspace::ProjectAutosave* autosave);
    // Told before Home opens one of its projects, for the line a reopened
    // calibration test starts with.
    void set_project_opening(std::function<void()> opening) { m_project_opening = std::move(opening); }
    // Stands in for Open…'s file dialog, for the integration harness: what it
    // returns is what the person chose, nothing for Cancel. Unset, the dialog.
    void set_file_chooser(std::function<wxArrayString()> choose) { m_choose_files = std::move(choose); }
    // Reads the signed-in account's credits again: a chat, a purchase or
    // Preferences may have changed them while Home was not on screen.
    void refresh_account();

    bool                      dark() const override;
    ProjectList               recent_projects() const override;
    std::vector<PrinterEntry> printers() const override;
    Snapshot::Onboarding onboarding() override;

    void open_project(const std::string& project_id) override;
    void new_project() override;
    void open_file() override;
    void open_dropped_files(const std::vector<DroppedFile>& files) override;
    void launch_monitor(const std::string& printer_id) override;
    void add_printer() override;
    void open_preferences() override;
    void check_for_updates() override;

    bool poll_onboarding() override;
    void start_sign_in() override;
    void cancel_sign_in() override;
    void sign_in_with_email(const std::string& email) override;
    void submit_sign_in_code(const std::string& code) override;
    void submit_sign_in_password(const std::string& password) override;
    void send_sign_in_code() override;
    void reopen_sign_in_browser() override;
    void open_sign_in_link(const std::string& link) override;
    void open_prices() override;
    void open_agent_door(const std::string& door) override;
    void leave_welcome() override;
    void step_back() override;
    void ask_agent_for_printer() override;
    void choose_printers_way(const std::string& way) override;
    std::string import_printers(const std::string& app, const std::vector<std::string>& printers) override;
    std::string assume_default_printer() override;
    void finish_import() override;
    void printer_form_message(const nlohmann::json& payload) override;
    void dismiss_account_card() override;

    std::string open_printer_settings(const std::string& printer_id) override;
    std::string ask_about_printer_settings(const std::string& printer_id) override;
    std::string connect_printer(const std::string& printer_id) override;
    std::string calibrate_printer(const std::string& printer_id) override;
    std::string rename_printer(const std::string& printer_id, const std::string& new_name) override;
    std::string remove_printer(const std::string& printer_id) override;

    nlohmann::json print_history() override;
    void           leave_print_history() override;
    void           open_in_chat(const std::string& project_id, const std::string& conversation_id,
                                const std::string& marker_id) override;
    void           export_gcode(const std::string& project_id, const std::string& hash, const std::string& beside) override;
    void           print_again(const std::string& project_id, const std::string& hash) override;
    bool           reports_offered() const override;
    std::optional<PrintReportPhoto> choose_report_photo() override;
    void           send_print_report(const PrintReportForm& form, std::function<void(const PrintReportAnswer&)> done) override;
    // Stands in for Export…'s file dialog, for the integration harness: the
    // path it returns is the one chosen, empty for Cancel. Unset, the dialog.
    void set_export_chooser(std::function<std::string(const std::string& name)> choose) { m_choose_export = std::move(choose); }
    // The same for a print report's photo.
    void set_report_photo_chooser(std::function<std::string()> choose) { m_choose_report_photo = std::move(choose); }

    // A Bambu Lab printer's monitor in a window of its own, showing the
    // device `device_id`; the header's printer menu opens it too.
    void open_monitor_window(const std::string& device_id);

    // A print host's own page (Mainsail, Fluidd, OctoPrint) in a window of
    // its own, for the named printer `name`; the project's selected printer
    // stays as it is. The header's printer menu opens it too.
    void open_printer_window(const std::string& name);

private:
    // Through the shell, which opens the files in a new project.
    void open_as_new_project(const wxArrayString& files);
    // What only the application knows about the records: preset names as the
    // person sees them, which card a printer is, and the exported files.
    HistoryFacts history_facts() const;
    // Whether an exported file still holds what was written. A file whose
    // size and time are as they were when it was last hashed is not hashed
    // again.
    std::string check_export(const Agent::GcodeDestination& exported) const;

    struct FileCheck
    {
        std::uintmax_t                  size{0};
        std::filesystem::file_time_type written;
        std::string                     hash;
    };
    class PrinterFormSession;
    OnboardingProgress onboarding_progress() const;
    OnboardingFacts onboarding_facts() const;
    bool agent_configured() const;
    void save_onboarding(const OnboardingProgress& progress);
    void complete_onboarding();
    // The Printers step has its printer: imported, added or assumed.
    void printer_chosen();
    // The app's folder on offer: the one the person picked for it with an
    // "Other ways" link, otherwise its own on this computer.
    OrcaProfileSource detect_import(const ImportApp& app);
    // An "Other ways" link opened this screen.
    void open_printers_way(PrintersWay way);
    const PrinterSetup::PrinterCatalog& profile_catalog();
    static std::vector<Snapshot::Onboarding::ImportItem> import_result_lines(const ImportApp& app,
                                                                             const std::vector<OrcaImportItem>& items);
    Agent::ICloudSignIn& sign_in();
    // Keeps the credits to the account signed in as `email`: another
    // account's are dropped and this one's read.
    void follow_account(const std::string& email);
    void request_account();

    MainFrame&                                         m_frame;
    Workspace::ProjectAutosave*                       m_autosave{nullptr};
    OpenConversation                                   m_open_conversation;
    StartCalibration                                   m_start_calibration;
    std::function<void()>                              m_project_opening;
    std::function<wxArrayString()>                     m_choose_files;
    std::function<std::string(const std::string&)>     m_choose_export;
    std::function<std::string()>                       m_choose_report_photo;
    std::map<std::string, wxWeakRef<PrinterWindow>>    m_printer_windows; // by printer name
    std::vector<wxWeakRef<MonitorWindow>>              m_monitor_windows;
    // The records read with the projects, for the printer cards' lines,
    // which the live refresh reads every second.
    mutable std::vector<HistoryProject>                m_history;
    mutable std::map<std::string, FileCheck>           m_file_checks; // by path
    std::map<std::string, std::string>                 m_report_photos; // path by the id the page holds
    // First run: the sign-in the welcome's card waits on, the account's
    // credits once read, and the add-printer form while the Printers step
    // shows it. Set when any of them changed what the page shows.
    Agent::CloudSignInPtr                              m_sign_in;
    std::string                                        m_sign_in_error;
    // That error is the server refusing this release of JusPrin.
    bool                                               m_sign_in_outdated{false};
    std::shared_ptr<Agent::IChatModelSource>           m_models;
    // The account m_account belongs to; empty when signed out.
    std::string                                        m_account_email;
    nlohmann::json                                     m_account;
    std::unique_ptr<PrinterFormSession>                m_printer_form;
    // The import's result while it is on screen, and the app it came from.
    std::optional<std::vector<OrcaImportItem>>         m_import_result;
    const ImportApp*                                   m_import_app{nullptr};
    // The folder the person picked for an app the app did not find, by
    // ImportApp::id; kept until the app quits.
    std::map<std::string, std::filesystem::path>       m_picked_folders;
    // Why the folder picked by the last "Other ways" link has nothing.
    std::string                                        m_way_problem;
    OrcaImporter                                       m_importer;
    bool                                               m_onboarding_changed{false};
    // An upload's answer can arrive after the backend is gone.
    std::shared_ptr<void>                              m_alive{std::make_shared<int>(0)};
};

}}}} // namespace Slic3r::GUI::JusPrin::Home
