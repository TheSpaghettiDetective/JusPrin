#pragma once

// One problem OrcaSlicer currently reports for a plate, and the list of them.
// Plain data, with no wx or OrcaSlicer types, so the chat context and the
// overlay's layout rules can be tested without the application.

#include <cstdint>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::PrintIssues {

// Where OrcaSlicer keeps the fact. See PrintIssueMonitor for how each is read.
enum class IssueSource : std::uint8_t {
    Validation,   // Print::validate
    Placement,    // a copy over the plate's edge
    PlateCheck,   // the filament and nozzle checks OrcaSlicer runs on a plate
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

    const PrintIssue* find(const std::string& id) const;
    // Whether anything a person would see differs, ignoring the generation.
    bool same_content(const PrintIssueSnapshot& other) const;
};

} // namespace Slic3r::GUI::JusPrin::PrintIssues
