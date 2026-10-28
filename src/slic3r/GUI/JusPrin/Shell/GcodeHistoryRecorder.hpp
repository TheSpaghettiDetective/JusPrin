#pragma once

// Writes the G-code records: where each G-code went when it left the project,
// and what the printer reported about printing it.
//
// It listens where OrcaSlicer already says a file left, and changes nothing
// about how it leaves:
// - an export: EVT_OUTPUT_EXPORTED, once the file is complete at its path
//   (OrcaSlicer's Export, and the export_file tool through the workspace);
// - a Bambu print or upload to storage: the print and send jobs' finished
//   events, which come only after the printer accepted the job;
// - a print host upload: the job queue's enqueue observer, then the queue
//   dialog's progress reaching 100.
// Which plates a send carries is not in those events, so the print and send
// actions that open OrcaSlicer's dialogs are observed on their way through.
//
// Printer outcomes: a Bambu printer's state is polled whether or not any
// screen shows it, and PrintOutcomeTracker decides which report belongs to
// which send. A finish may belong to a project that is not open; that
// project's document is written in place.
//
// GUI thread only, except the print host observer, which only queues work
// back onto the GUI thread.

#include "slic3r/GUI/JusPrin/Agent/ManufacturingHistory.hpp"
#include "slic3r/GUI/JusPrin/Agent/PrintOutcomeTracker.hpp"
#include "slic3r/GUI/JusPrin/Home/PrintHistory.hpp"

#include "slic3r/GUI/PrintHostDialogs.hpp"

#include <wx/event.h>
#include <wx/timer.h>
#include <wx/weakref.h>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class wxCommandEvent;

namespace Slic3r::GUI {
class Plater;
}

namespace Slic3r::GUI::JusPrin::Agent {
class ProjectPersistence;
class ProjectStateDocument;
}

namespace Slic3r::GUI::JusPrin::Workspace {
class IWorkspace;
}

namespace Slic3r::GUI::JusPrin {

class GcodeHistoryRecorder final : public wxEvtHandler
{
public:
    // Each saved project's id and its G-code records, the open one included.
    using ProjectRecords = std::function<std::vector<std::pair<std::string, std::vector<Agent::GcodeRecord>>>()>;

    GcodeHistoryRecorder(Plater& plater, Agent::ProjectPersistence& persistence, Workspace::IWorkspace& workspace,
                         std::filesystem::path managed_root, ProjectRecords project_records);
    ~GcodeHistoryRecorder() override;

    GcodeHistoryRecorder(const GcodeHistoryRecorder&) = delete;
    GcodeHistoryRecorder& operator=(const GcodeHistoryRecorder&) = delete;

    // Told after every record written, the open project's or another's.
    void set_changed_listener(std::function<void()> listener) { m_changed = std::move(listener); }
    // The project's kept copy of a G-code; empty once it is no longer kept.
    std::string kept_copy(const std::string& project_id, const std::string& hash) const;
    // Writes the kept copy to `target` and records that export of the G-code.
    // Throws when the project keeps no copy or the file cannot be written.
    void export_kept_copy(const std::string& project_id, const std::string& hash, const std::filesystem::path& target);
    // Whether the kept copy is there and still holds the G-code's bytes.
    bool kept_copy_intact(const std::string& project_id, const std::string& hash) const;

    // Print again sends a recorded G-code's kept file, never a fresh slice,
    // through OrcaSlicer's own send dialog for the printer selected now: a
    // Bambu printer takes the kept package (Select Machine), a print host
    // with OrcaSlicer's standard upload dialog takes the kept copy. The
    // person presses Send there; nothing in the dialog is set.
    enum class PrintAgain {
        Ready,
        NotKept,           // the G-code's copy is gone: slice again
        NotForThisPrinter, // kept, but not in the form the selected printer takes
        NoPrinter,         // no printer connection is set up
        Changed            // the kept file no longer holds what was kept
    };
    PrintAgain print_again_state(const std::string& project_id, const Agent::GcodeRecord& record) const;
    // Opens the send dialog; it returns once the dialog is closed. Throws
    // unless the state is Ready, which the caller checks first.
    void print_again(const std::string& project_id, const std::string& hash);

private:
    // One plate's G-code as it leaves: what the record says about it and the
    // file whose bytes are its identity.
    struct Leaving
    {
        Agent::GcodeRecord facts;
        std::string        gcode_path;
    };
    // A Print again on its way: the record it sends again, and the hash its
    // kept package must have (empty for a print host, which takes the copy).
    struct Reprint
    {
        std::string project_id;
        std::string hash;
        std::string package_hash;
    };
    struct PendingUpload
    {
        std::vector<Leaving>    leaving;
        std::optional<Reprint>  reprint;
        Agent::GcodeDestination destination;
    };

    // `plate` is a plate index or PLATE_ALL_IDX. `uploaded` is the package a
    // Bambu print sent, kept beside a single plate's copy.
    std::vector<Leaving> leaving(int plate) const;
    void record(const std::vector<Leaving>& leaving, Agent::GcodeDestination destination,
                const std::optional<Agent::KeptPackage>& uploaded = std::nullopt);
    void record_reprint(const Reprint& reprint, Agent::GcodeDestination destination);
    void await_start(const std::string& project_id, const std::string& hash, const std::string& destination_id,
                     const Agent::GcodeDestination& destination);
    Agent::GcodeRecord project_record(const std::string& project_id, const std::string& hash) const;
    std::string        project_dir(const std::string& project_id) const;
    // Whether `path` hashes to `expected`; a file whose size and time are as
    // at its last hash is not hashed again.
    bool intact(const std::string& path, const std::string& expected) const;
    void send_again_to_print_host(const std::string& project_id, const std::string& hash, const std::string& copy,
                                  const Agent::GcodeRecord& record);
    void update_project(const std::string& project_id, const std::function<void(Agent::ProjectStateDocument&)>& edit);

    void on_print_current(wxEvent& event);
    void on_print_all(wxEvent& event);
    void on_print_elsewhere(wxEvent& event);
    void on_output_exported(wxCommandEvent& event);
    void on_bambu_print_finished(wxCommandEvent& event);
    void on_bambu_send_finished(wxCommandEvent& event);
    void record_bambu_job(const wxCommandEvent& event, bool starts_print);
    // The upload queue and its dialog are made after the shell is installed,
    // and made again with a new main frame: they are hooked by the print
    // action that may use them.
    void hook_print_host_queue();
    void unhook_print_host_queue();
    void on_print_host_queued(std::size_t id, std::string host, std::string file_name, bool start);
    void on_print_host_progress(wxEvent& event);
    void on_print_host_ended(wxEvent& event);
    void on_poll(wxTimerEvent& event);
    void follow_unfinished_prints();

    Plater&                    m_plater;
    Agent::ProjectPersistence& m_persistence;
    Workspace::IWorkspace&     m_workspace;
    std::filesystem::path      m_managed_root;
    ProjectRecords             m_project_records;

    // The plates the last print or send action carries; nothing when the
    // last action sends no G-code of this project (a print from the
    // printer's own storage, a farm send).
    std::optional<int>                   m_send_plates;
    // The Print again whose dialog is open; the job it sends is that record's.
    std::optional<Reprint>               m_reprint;
    bool                                 m_opening_reprint{false};
    std::map<std::size_t, PendingUpload> m_uploads;
    struct FileCheck
    {
        std::uintmax_t                  size{0};
        std::filesystem::file_time_type written;
        std::string                     hash;
    };
    mutable std::map<std::string, FileCheck> m_file_checks; // by path
    PrintHostJobQueue*                   m_print_host_queue{nullptr};
    wxWeakRef<PrintHostQueueDialog>      m_print_host_dialog;

    Agent::PrintOutcomeTracker m_tracker;
    bool                       m_followed_saved_prints{false};
    wxTimer                    m_poll;
    std::function<void()>      m_changed;
};

// The jobs the account's Bambu printers are running now, keyed as the records
// key their prints.
std::vector<Home::LivePrint> live_prints();

} // namespace Slic3r::GUI::JusPrin
