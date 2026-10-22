#pragma once

// What the Check print window's report says about one sliced plate, as plain
// data. Like the other shell models it has no wx and no Orca types: it reads
// a Workspace::SliceReport and decides which checks have something to say,
// in which order, and which found nothing. The panel only formats it.

#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin {

// One check slice_report computes. The order of the enumerators is the
// tie-break between two sections of equal severity: the earlier one is the
// more consequential, the one that ruins a print rather than its looks.
enum class PrintCheck : std::uint8_t {
    OutsidePlate,  // a toolpath outside the printable area
    CrossingPaths, // OrcaSlicer's path conflict check
    OrcaWarnings,  // OrcaSlicer's own warnings about this slice
    Supports,
    Islands,       // slices printed in the air
    Seams,         // seams on faces the person marked
    FirstLayer,    // contact with the plate; informational, never "clean"
};

// A row's severity: the colour of its dot, or no dot.
enum class CheckSeverity : std::uint8_t { None, Warning, Critical };

// What a row says. The panel turns each into a sentence; the fields of
// CheckRow it reads are listed beside each.
enum class CheckRowKind : std::uint8_t {
    ToolpathOutside,   // (no fields)
    PathConflict,      // message: OrcaSlicer's words
    OrcaWarning,       // message, object (may be empty: the whole plate)
    SupportInRegion,   // object, region_label, area, layers, z
    SupportInHole,     // object, region_label (how the check names the hole), area, layers, z
    SupportClear,      // count: the support's layers; no contact found
    UnsupportedIsland, // object, area, z
    SeamsOnRegion,     // object, region_label, region_kind, count
    FirstLayerContact, // object, area, brim
};

struct CheckRow
{
    CheckRowKind                       kind{CheckRowKind::OrcaWarning};
    CheckSeverity                      severity{CheckSeverity::None};
    std::string                        message;
    // OrcaSlicer's condition for a warning: empty, or "timelapse" for one
    // that matters only to a print recording a timelapse.
    std::string                        applies_when;
    std::string                        object;
    std::optional<Workspace::ObjectId> object_id;
    std::string                        region_label;
    std::string                        region_kind;
    double                             area{0};
    std::size_t                        layers{0};
    std::size_t                        count{0};
    bool                               brim{false};
    // The height the row is about, when it has one: selecting the row moves
    // the layer slider there.
    std::optional<double>              z;
};

struct CheckSection
{
    PrintCheck            check{PrintCheck::OrcaWarnings};
    CheckSeverity         worst{CheckSeverity::None};
    std::vector<CheckRow> rows;
    // Supports: the objects given support, and the most layers any one has.
    std::size_t           support_objects{0};
    std::size_t           support_layers{0};
    // Rows were capped by slice_report.
    bool                  truncated{false};
};

struct FilamentLine
{
    std::size_t extruder{0}; // zero-based, as slice_report numbers them
    double      grams{0};
    double      length_mm{0};
};

struct CheckPrintSummary
{
    std::uint32_t             print_time_seconds{0};
    std::vector<FilamentLine> filaments;
    bool                      has_cost{false};
    double                    cost{0};
    std::uint32_t             filament_changes{0};
};

struct CheckPrintReportView
{
    bool                      valid{false};
    CheckPrintSummary         summary;
    // In the order the panel draws them.
    std::vector<CheckSection> sections;
    // The checks that ran and found nothing, in the closing line's order.
    std::vector<PrintCheck>   clean;
    // False when the slice generated no support: the closing line then says
    // "supports (none generated)" rather than "supports".
    bool                      supports_generated{false};
};

// The regions are the project's, so a row can use the name the person gave a
// region rather than its id.
CheckPrintReportView build_check_print_report(const Workspace::SliceReport&            report,
                                              const std::vector<Workspace::RegionRecord>& regions);

// The order the sections are drawn in: supports first whenever the slice
// generated any, then by worst severity, ties going to the more consequential
// check. Exposed so the rule is tested on its own.
void order_check_sections(std::vector<CheckSection>& sections, bool supports_generated);

// The closing line's order, which is also every check that can be "clean".
const std::vector<PrintCheck>& closing_line_order();

// The scroll offset that shows a row spanning [top, bottom) of the content
// with `margin` around it, moving as little as possible from `scroll`, kept
// within the content. All in pixels; `viewport` is the visible height.
int scroll_to_show(int top, int bottom, int scroll, int viewport, int margin, int content);

// A scrollbar thumb on a track `track` long: its offset from the track's
// start and its length, never shorter than `min_thumb` or longer than the track.
struct ScrollThumb { int offset{0}; int length{0}; };
ScrollThumb scrollbar_thumb(int track, int viewport, int content, int scroll, int min_thumb);

// The layer whose top is the first at or above `z` (mm), or the top layer
// when `z` is above them all; nullopt for an empty list. Layer numbers in the
// caption count from one.
std::optional<std::size_t> layer_index_at(const std::vector<double>& layer_tops, double z);

} // namespace Slic3r::GUI::JusPrin
