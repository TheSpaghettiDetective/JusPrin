#pragma once

// What the Project tab shows, as plain data. Like LeftPaneModel it has no wx
// and no Orca types, and it states only what the owners recorded: the model's
// metadata comes from the 3MF's project details, the print history from the
// physical-print ledger. A field nobody recorded is absent here, never blank,
// zero, or guessed, so the pane has nothing to omit by hand.

#include "slic3r/GUI/JusPrin/Agent/ManufacturingHistory.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin {

// The model's own words about itself, as 3MF carries them. The saved project's
// name is a different fact and is never a fallback for `title`.
struct ModelInfoView
{
    std::string title, designer, license, copyright, origin, description;
    std::string profile_title, profile_description;

    bool empty() const
    {
        return title.empty() && designer.empty() && license.empty() && copyright.empty() && origin.empty() &&
               description.empty() && profile_title.empty() && profile_description.empty();
    }
};

struct AttachmentGroup
{
    std::string                              folder; // Orca's category, such as "Model Pictures"
    std::vector<Workspace::ProjectAttachment> files;
};

// The Details view: only sections the file backs with data.
struct ProjectDetailsView
{
    ModelInfoView                model;
    std::vector<AttachmentGroup> attachments;
    bool                         attachments_truncated{false};

    bool empty() const { return model.empty() && attachments.empty(); }
};

ProjectDetailsView build_details_view(const Workspace::ProjectDetails& details);

enum class PrintOutcome : std::uint8_t { Completed, Failed, Cancelled, Unrecorded };

struct PrintHistoryEntry
{
    std::string  id;
    PrintOutcome outcome{PrintOutcome::Unrecorded};
    std::string  recorded_outcome;
    // Verbatim from the ledger (ISO 8601 UTC); the pane formats them.
    std::string  started_at, ended_at;
    // Present only when both timestamps parse and the end is not before the start.
    std::optional<std::int64_t> duration_seconds;
    std::string  plate_name, printer, material, failure;
    std::optional<int> stopped_percent;
    // The slicer's estimate when the build was made. It is a prediction, not a
    // measurement of the print, and a zero means nobody recorded it.
    std::optional<double> estimated_seconds, estimated_grams, estimated_cost;
};

struct PrintHistoryView
{
    std::size_t                    total{0};
    std::vector<PrintHistoryEntry> detailed; // newest first
    // Prints that were counted but left no facts to show.
    std::size_t                    count_only{0};
};

// `records` is the ledger as stored (oldest first). A record with no outcome,
// time, plate, printer or material has nothing to show beyond that it
// happened, so it is counted and not drawn as an empty row.
PrintHistoryView build_print_history(const std::vector<Agent::PhysicalPrintRecord>& records);

// Seconds between two ISO 8601 UTC timestamps ("2026-08-30T01:02:03Z"), or
// nullopt when either does not parse or the end precedes the start.
std::optional<std::int64_t> seconds_between(const std::string& start, const std::string& end);

} // namespace Slic3r::GUI::JusPrin
