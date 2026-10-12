#pragma once

// What OrcaSlicer currently has to say about the active plate.
//
// OrcaSlicer's own notification list is the source of truth. Inside the shell
// its notifications are held but not drawn; this reads the list back and
// sorts it by NoticeKinds: print problems become the issue list, results and
// running work become the short confirmations on the canvas.
//
// A notification carries a kind, a level and words, and no object. So for the
// kinds whose check OrcaSlicer lets anyone ask again, that check is asked
// once more for the one thing the notification lacks, the object or copy it
// is about (Print::validate, a copy's print-volume state, the nozzle-reach
// check, the print's step warnings). Such an entry exists only while
// OrcaSlicer holds a notification of its kind.
//
// Two places are read whether or not a notification exists, because
// OrcaSlicer's notifications do not cover them in time: the slice result,
// which is given its notifications only when Check print loads it, and the
// reason a slice failed, for which a critical failure shows a dialog and
// leaves no notification.
//
// Nothing here decides whether something is a problem, and nothing is kept
// alive on its own. While OrcaSlicer holds an edit it has not yet applied to
// the print, the last issues are kept and marked stale. OrcaSlicer checks
// only the active plate, so that is the only plate reported.
//
// GUI thread only.

#include "PrintIssue.hpp"

#include <wx/event.h>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace Slic3r {
class Print;
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
    // The same moment, for the work feed.
    void set_work_callback(std::function<void()> callback) { m_work_changed = std::move(callback); }

    // Reads the native state now, for a press that must act on what is true
    // at the press.
    void refresh_now();

    // Closes OrcaSlicer's own notifications of one kind, as their close
    // button did. The confirmation goes when OrcaSlicer lets go of them.
    void dismiss(int kind);

    // The person's "ignore this warning", and its undo. Nothing is closed in
    // OrcaSlicer: the warning stays in its list and in this one, marked, and
    // out of the count. See mark_ignored for how long an ignore lasts.
    void set_ignored(const std::string& issue_id, bool ignored);

    // How long OrcaSlicer's validation took the last time it was asked, and
    // how often it has been: the cost of asking again, for tests to watch.
    double        last_validate_ms() const { return m_last_validate_ms; }
    std::uint64_t validate_runs() const { return m_validate_runs; }
    // How often the list was rebuilt, against how often it was only compared.
    std::uint64_t rebuilds() const { return m_rebuilds; }
    std::uint64_t comparisons() const { return m_comparisons; }

private:
    struct SliceFailure
    {
        std::string                message;
        std::vector<std::uint64_t> objects; // ModelObject ids
        bool                       critical{false};
        // The project was edited after the slice failed.
        bool                       outdated{false};
    };
    struct ResultFindings
    {
        const void*             result{nullptr};
        unsigned int            result_id{0};
        std::vector<PrintIssue> issues;
    };
    // Everything a rebuild depends on that can be read for almost nothing.
    struct Seen
    {
        std::uint64_t session{0}, plate{0};
        bool          checked{false}, slicing{false}, sliced{false};
        std::size_t   notices{0};
        bool operator==(const Seen& other) const
        {
            return session == other.session && plate == other.plate && checked == other.checked && slicing == other.slicing &&
                   sliced == other.sliced && notices == other.notices;
        }
    };

    // The notification manager's turn: compare, and rebuild only on a change.
    void compare();
    Seen look() const;
    void rebuild();
    std::vector<Notice> held() const;
    void find_placement(PartPlate& plate, const std::vector<Notice>& held, std::vector<PrintIssue>& found) const;
    void find_validation(PartPlate& plate, Print& print, const std::vector<Notice>& held, std::vector<PrintIssue>& found);
    void find_steps(PartPlate& plate, const Print& print, std::vector<PrintIssue>& found) const;
    void read_result(PartPlate& plate, const Print& print, std::vector<PrintIssue>& found);
    void read_failure(PartPlate& plate, std::vector<PrintIssue>& found) const;

    void on_process_completed(SlicingProcessCompletedEvent& event);

    Plater&               m_plater;
    PrintIssueSnapshot    m_snapshot;
    std::function<void()> m_changed, m_work_changed;
    Seen                  m_seen;
    bool                  m_looked{false};
    // Validation as of the last time the print was in step with the project
    // and no slice was running.
    std::vector<PrintIssue> m_validation;
    // What the current slice result says, worked out once per result: it
    // means walking every move.
    ResultFindings        m_result;
    std::map<std::uint64_t, SliceFailure> m_failures; // by plate id
    // By plate id, so looking at another plate does not forget this one's.
    std::map<std::uint64_t, std::set<std::string>> m_ignored;
    bool                  m_was_slicing{false};
    double                m_last_validate_ms{0};
    std::uint64_t         m_validate_runs{0}, m_rebuilds{0}, m_comparisons{0};
};

} // namespace Slic3r::GUI::JusPrin::PrintIssues
