#pragma once

// What OrcaSlicer currently says is wrong with the active plate, read from
// OrcaSlicer's own state every time that state may have changed.
//
// Nothing here decides whether something is a problem. Each entry is the
// answer of a native check asked again (Print::validate, the plate's filament
// checks), state OrcaSlicer already stores (a copy's print-volume state, the
// print's step warnings, the slice result), or the one fact only an event
// carries (why a slice failed).
//
// The monitor keeps no issue alive on its own. An entry exists while its
// native source still reports it, and is marked stale while OrcaSlicer holds
// an edit it has not yet applied to the print. OrcaSlicer validates only the
// active plate, so that is the only plate reported.
//
// GUI thread only.

#include "PrintIssue.hpp"
#include "slic3r/GUI/JusPrin/Workspace/ProjectState.hpp"

#include <wx/event.h>
#include <wx/timer.h>

#include <functional>
#include <map>
#include <memory>

namespace Slic3r {
class Print;
class SlicingStatusEvent;
class SlicingProcessCompletedEvent;
namespace GUI {
class Plater;
class PartPlate;
}
} // namespace Slic3r

namespace Slic3r::GUI::JusPrin::PrintIssues {

class PrintIssueMonitor final
{
public:
    explicit PrintIssueMonitor(Plater& plater);
    ~PrintIssueMonitor();

    PrintIssueMonitor(const PrintIssueMonitor&) = delete;
    PrintIssueMonitor& operator=(const PrintIssueMonitor&) = delete;

    const PrintIssueSnapshot& snapshot() const { return m_snapshot; }
    // Called after the snapshot changed. One listener: the overlay.
    void set_changed_callback(std::function<void()> callback) { m_changed = std::move(callback); }

    // Reads the native state now instead of on the next loop turn, for a
    // press that must act on what is true at the press.
    void refresh_now();

    // How long OrcaSlicer's validation took the last time it was asked, and
    // how often it has been: the cost of asking again, for tests to watch.
    double        last_validate_ms() const { return m_last_validate_ms; }
    std::uint64_t validate_runs() const { return m_validate_runs; }

private:
    struct SliceFailure
    {
        std::string                message;
        std::vector<std::uint64_t> objects; // ModelObject ids
        bool                       critical{false};
        std::uint64_t              project_change{0};
    };
    struct ResultFindings
    {
        const void*             result{nullptr};
        unsigned int            result_id{0};
        std::vector<PrintIssue> issues;
    };

    void schedule();
    void recompute();
    void collect_placement(PartPlate& plate, std::vector<PrintIssue>& issues) const;
    void collect_plate_checks(PartPlate& plate, std::vector<PrintIssue>& issues) const;
    void collect_validation(PartPlate& plate, Print& print, std::vector<PrintIssue>& issues);
    void collect_steps(PartPlate& plate, const Print& print, std::vector<PrintIssue>& issues) const;
    void collect_result(PartPlate& plate, const Print& print, std::vector<PrintIssue>& issues);
    void collect_failure(PartPlate& plate, std::vector<PrintIssue>& issues) const;

    void on_slice_status(wxCommandEvent& event);
    void on_timer(wxTimerEvent& event);
    void on_slicing_update(SlicingStatusEvent& event);
    void on_process_completed(SlicingProcessCompletedEvent& event);

    Plater&                  m_plater;
    ProjectStateSubscription m_project_subscription;
    PrintIssueSnapshot       m_snapshot;
    std::function<void()>    m_changed;
    // Validation as of the last time the print was in step with the project
    // and no slice was running.
    std::vector<PrintIssue>  m_validation;
    // What the current slice result says, worked out once per result: it
    // means walking every move.
    ResultFindings           m_result;
    std::map<std::uint64_t, SliceFailure> m_failures; // by plate id
    std::uint64_t            m_project_changes{0};
    bool                     m_scheduled{false};
    bool                     m_was_slicing{false};
    double                   m_last_validate_ms{0};
    std::uint64_t            m_validate_runs{0};
    // Expires with the monitor, so a queued recompute is dropped.
    std::shared_ptr<PrintIssueMonitor*> m_self;
};

} // namespace Slic3r::GUI::JusPrin::PrintIssues
