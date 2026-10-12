#pragma once

// What OrcaSlicer currently has to say: the notifications it holds, and the
// print problems among them with the object each is about. Plain data, with
// no wx or OrcaSlicer types, so the chat context, the overlay's layout rules
// and the rule that turns notifications into issues can be tested without the
// application.

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::PrintIssues {

// What JusPrin does with one kind of OrcaSlicer notification. The table that
// assigns every kind is in NoticeKinds.
enum class NoticeRoute : std::uint8_t {
    Issue,    // a print problem: the issue list
    Result,   // the outcome of something the person just did: a short confirmation
    Progress, // work that is still running
    App,      // application or account state, which has no place in a project yet
    NotShown
};
enum class NoticeLevel : std::uint8_t { Info, Warning, Error };

// One notification OrcaSlicer holds, as its notification manager reports it.
struct Notice
{
    int         kind{0};   // OrcaSlicer's NotificationType
    std::string kind_name; // its enumerator, for ids and for the agent
    NoticeRoute route{NoticeRoute::NotShown};
    NoticeLevel level{NoticeLevel::Info};
    std::string text;      // OrcaSlicer's words, unchanged
    // The words without the "Error:" or "Warning:" line OrcaSlicer puts first.
    std::string body;
    // OrcaSlicer may hold several of this kind at once, told apart by text.
    bool        several{false};

    bool operator==(const Notice& other) const;
};

// Where an issue's target came from. See PrintIssueMonitor for how each is read.
enum class IssueSource : std::uint8_t {
    Notice,       // the notification itself: OrcaSlicer named no object anyone can read
    Validation,   // Print::validate
    Placement,    // a copy over the plate's edge, an object out of a nozzle's reach
    SliceStep,    // a warning a slicing step left on the print or an object
    SliceResult,  // what a finished slice says about itself
    SliceFailure  // why the last slice did not finish
};
enum class IssueSeverity : std::uint8_t { Warning, Blocker };

const char* source_name(IssueSource source);
const char* severity_name(IssueSeverity severity);

struct PrintIssue
{
    // Built from the native source's own identifiers (check type, setting
    // key, step, message id, object ids), never from the message text.
    std::string   id;
    IssueSource   source{IssueSource::Validation};
    IssueSeverity severity{IssueSeverity::Warning};
    std::string   code;    // the native check or step, empty when OrcaSlicer has none
    std::string   setting; // the setting key OrcaSlicer attached, if any
    std::string   message; // OrcaSlicer's words, unchanged
    std::uint64_t plate{0};
    std::uint64_t object{0};   // ModelObject id; 0 for a plate-wide finding
    std::uint64_t instance{0}; // ModelInstance id; 0 when OrcaSlicer names only the object
    std::string   object_name;
    // The native source no longer vouches for it: an edit is waiting to be
    // applied, or the step it came from was invalidated.
    bool          stale{false};
    // The notification kind this entry answers for, or -1. A lookup's entry
    // exists only while OrcaSlicer holds a notification of that kind, unless
    // it is `unprompted`.
    int           answers{-1};
    // Read from a place OrcaSlicer's notifications do not cover in time: the
    // slice result, which gets its notifications only when Check print loads
    // it, and a failed slice, which gets none when OrcaSlicer shows a dialog.
    bool          unprompted{false};
    // The person asked not to be told about it. It is still OrcaSlicer's
    // finding and still listed; it is only left out of the count.
    bool          ignored{false};

    bool operator==(const PrintIssue& other) const;
    bool operator!=(const PrintIssue& other) const { return !(*this == other); }
};

struct PrintIssueSnapshot
{
    std::uint64_t session{0};
    // Advances whenever the list or its freshness changes.
    std::uint64_t generation{0};
    std::uint64_t plate{0};
    // False while OrcaSlicer holds an edit it has not validated yet. An empty
    // list with checked == false is "not known", never "all clear".
    bool          checked{false};
    bool          slicing{false};
    bool          sliced{false};
    std::vector<PrintIssue> issues;
    // What OrcaSlicer holds that is not a print problem and has a place here:
    // results and running work, newest last.
    std::vector<Notice>     notices;

    const PrintIssue* find(const std::string& id) const;
    // Whether anything a person would see differs, ignoring the generation.
    bool same_content(const PrintIssueSnapshot& other) const;
};

// The print problems, from what OrcaSlicer holds and what the lookups found.
// OrcaSlicer's list decides what exists: a found entry is kept only while a
// notification of the kind it answers for is held, unless it is unprompted.
// A held print-problem notification that no kept entry answers for becomes a
// plate-wide issue in OrcaSlicer's words.
std::vector<PrintIssue> issues_from(const std::vector<Notice>& held, std::vector<PrintIssue> found, std::uint64_t plate);

// The person's "ignore this warning", applied to what OrcaSlicer reports.
// Only a warning can be ignored: OrcaSlicer refuses to print over a blocker,
// and hiding one would leave Print off with nothing saying why. `settled` is
// a list OrcaSlicer vouches for; an id it no longer reports as a warning is
// forgotten then, so a warning that comes back is shown again.
void mark_ignored(std::vector<PrintIssue>& issues, std::set<std::string>& ignored, bool settled);

} // namespace Slic3r::GUI::JusPrin::PrintIssues
