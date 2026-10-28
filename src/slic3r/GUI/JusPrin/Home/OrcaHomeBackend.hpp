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
#include "PrintHistory.hpp"
#include "PrinterWindow.hpp"

#include <wx/arrstr.h>
#include <wx/weakref.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>

namespace Slic3r { namespace GUI {
class MainFrame;
}} // namespace Slic3r::GUI
namespace Slic3r::GUI::JusPrin::Workspace { class ProjectAutosave; }

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
    void set_autosave(Workspace::ProjectAutosave* autosave) { m_autosave = autosave; }
    // Told before Home opens one of its projects, for the line a reopened
    // calibration test starts with.
    void set_project_opening(std::function<void()> opening) { m_project_opening = std::move(opening); }
    // Stands in for Open…'s file dialog, for the integration harness: what it
    // returns is what the person chose, nothing for Cancel. Unset, the dialog.
    void set_file_chooser(std::function<wxArrayString()> choose) { m_choose_files = std::move(choose); }

    bool                      dark() const override;
    ProjectList               recent_projects() const override;
    std::vector<PrinterEntry> printers() const override;

    void open_project(const std::string& project_id) override;
    void new_project() override;
    void open_file() override;
    void open_dropped_files(const std::vector<DroppedFile>& files) override;
    void launch_monitor(const std::string& printer_id) override;
    void add_printer() override;
    void open_preferences() override;

    std::string open_printer_settings(const std::string& printer_id) override;
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
    // Stands in for Export…'s file dialog, for the integration harness: the
    // path it returns is the one chosen, empty for Cancel. Unset, the dialog.
    void set_export_chooser(std::function<std::string(const std::string& name)> choose) { m_choose_export = std::move(choose); }

private:
    // A print host's own page (Mainsail, Fluidd, OctoPrint) in a window of
    // its own; the project's selected printer stays as it is.
    void open_printer_window(const std::string& name);
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

    MainFrame&                                         m_frame;
    Workspace::ProjectAutosave*                       m_autosave{nullptr};
    OpenConversation                                   m_open_conversation;
    StartCalibration                                   m_start_calibration;
    std::function<void()>                              m_project_opening;
    std::function<wxArrayString()>                     m_choose_files;
    std::function<std::string(const std::string&)>     m_choose_export;
    std::map<std::string, wxWeakRef<PrinterWindow>>    m_printer_windows; // by printer name
    // The records read with the projects, for the printer cards' lines,
    // which the live refresh reads every second.
    mutable std::vector<HistoryProject>                m_history;
    mutable std::map<std::string, FileCheck>           m_file_checks; // by path
};

}}}} // namespace Slic3r::GUI::JusPrin::Home
