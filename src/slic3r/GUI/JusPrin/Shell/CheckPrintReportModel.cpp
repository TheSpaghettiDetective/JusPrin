#include "CheckPrintReportModel.hpp"

#include <algorithm>

namespace Slic3r::GUI::JusPrin {

namespace {

// A layer top read back from G-code carries float noise; a finding at the
// layer's own height belongs to that layer, not the next.
constexpr double kLayerTolerance = 1e-4;

// Seams on these faces are the problem the person marked them for; seams on
// a "seam here" or "hidden" face are where they were asked to go.
bool seams_unwanted_on(const std::string& kind)
{
    return kind == "visible" || kind == "seam_forbidden" || kind == "smooth_face";
}

std::string region_label(const std::vector<Workspace::RegionRecord>& regions, const std::string& id)
{
    for (const Workspace::RegionRecord& region : regions)
        if (region.id == id)
            return region.label.empty() ? region.id : region.label;
    return id;
}

CheckSeverity worst_of(const std::vector<CheckRow>& rows)
{
    CheckSeverity worst = CheckSeverity::None;
    for (const CheckRow& row : rows)
        worst = std::max(worst, row.severity);
    return worst;
}

CheckSection make_section(PrintCheck check, std::vector<CheckRow> rows, bool truncated = false)
{
    // The most severe rows first; equal rows keep slice_report's order.
    std::stable_sort(rows.begin(), rows.end(),
                     [](const CheckRow& a, const CheckRow& b) { return a.severity > b.severity; });
    CheckSection section;
    section.check     = check;
    section.worst     = worst_of(rows);
    section.rows      = std::move(rows);
    section.truncated = truncated;
    return section;
}

} // namespace

const std::vector<PrintCheck>& closing_line_order()
{
    static const std::vector<PrintCheck> order{PrintCheck::Supports,      PrintCheck::Seams,
                                               PrintCheck::Islands,       PrintCheck::CrossingPaths,
                                               PrintCheck::OutsidePlate,  PrintCheck::OrcaWarnings};
    return order;
}

void order_check_sections(std::vector<CheckSection>& sections, bool supports_generated)
{
    std::stable_sort(sections.begin(), sections.end(), [supports_generated](const CheckSection& a, const CheckSection& b) {
        if (supports_generated && (a.check == PrintCheck::Supports) != (b.check == PrintCheck::Supports))
            return a.check == PrintCheck::Supports;
        if (a.worst != b.worst)
            return a.worst > b.worst;
        return a.check < b.check;
    });
}

int scroll_to_show(int top, int bottom, int scroll, int viewport, int margin, int content)
{
    if (top - margin < scroll)
        scroll = top - margin;
    else if (bottom + margin > scroll + viewport)
        scroll = bottom + margin - viewport;
    return std::clamp(scroll, 0, std::max(0, content - viewport));
}

ScrollThumb scrollbar_thumb(int track, int viewport, int content, int scroll, int min_thumb)
{
    if (track <= 0 || content <= viewport || content <= 0)
        return {0, std::max(0, track)};
    const int length = std::min(track, std::max(min_thumb, track * viewport / content));
    const int range  = content - viewport;
    return {(track - length) * std::clamp(scroll, 0, range) / range, length};
}

std::optional<std::size_t> layer_index_at(const std::vector<double>& layer_tops, double z)
{
    if (layer_tops.empty())
        return std::nullopt;
    const auto found = std::lower_bound(layer_tops.begin(), layer_tops.end(), z - kLayerTolerance);
    if (found == layer_tops.end())
        return layer_tops.size() - 1;
    return static_cast<std::size_t>(found - layer_tops.begin());
}

CheckPrintReportView build_check_print_report(const Workspace::SliceReport&            report,
                                              const std::vector<Workspace::RegionRecord>& regions)
{
    CheckPrintReportView view;
    view.valid = report.valid;
    if (!report.valid)
        return view;

    view.summary.print_time_seconds = report.print_time_seconds;
    view.summary.has_cost           = report.has_cost;
    view.summary.cost               = report.total_cost;
    view.summary.filament_changes   = report.filament_changes;
    for (const Workspace::SliceFilamentUse& use : report.filaments)
        view.summary.filaments.push_back({use.extruder, use.grams, use.length_mm});

    std::vector<CheckSection> sections;
    const auto clean = [&view](PrintCheck check) { view.clean.push_back(check); };

    if (report.toolpath_outside) {
        CheckRow row;
        row.kind     = CheckRowKind::ToolpathOutside;
        row.severity = CheckSeverity::Critical;
        sections.push_back(make_section(PrintCheck::OutsidePlate, {row}));
    } else {
        clean(PrintCheck::OutsidePlate);
    }

    if (!report.conflict.empty()) {
        CheckRow row;
        row.kind     = CheckRowKind::PathConflict;
        row.severity = CheckSeverity::Critical;
        row.message  = report.conflict;
        sections.push_back(make_section(PrintCheck::CrossingPaths, {row}));
    } else {
        clean(PrintCheck::CrossingPaths);
    }

    if (!report.findings.empty()) {
        std::vector<CheckRow> rows;
        for (const Workspace::SliceFinding& finding : report.findings) {
            // OrcaSlicer can hold one warning twice for the same object.
            const bool repeated = std::any_of(rows.begin(), rows.end(), [&finding](const CheckRow& row) {
                return row.message == finding.message && row.object == finding.object;
            });
            if (repeated)
                continue;
            CheckRow row;
            row.kind         = CheckRowKind::OrcaWarning;
            row.severity     = finding.critical ? CheckSeverity::Critical : CheckSeverity::Warning;
            row.message      = finding.message;
            row.applies_when = finding.applies_when;
            row.object       = finding.object;
            rows.push_back(std::move(row));
        }
        sections.push_back(make_section(PrintCheck::OrcaWarnings, std::move(rows)));
    } else {
        clean(PrintCheck::OrcaWarnings);
    }

    if (report.supports) {
        view.supports_generated = report.supports->generated;
        if (report.supports->generated) {
            std::vector<CheckRow> rows;
            for (const Workspace::SupportContact& contact : report.supports->contacts) {
                CheckRow row;
                // A region is the person's own mark; a hole is the check's guess.
                const bool marked = !contact.region_id.empty();
                row.kind          = marked ? CheckRowKind::SupportInRegion : CheckRowKind::SupportInHole;
                row.severity      = marked ? CheckSeverity::Critical : CheckSeverity::Warning;
                row.object        = contact.object_name;
                row.object_id     = contact.object;
                row.region_label  = marked ? region_label(regions, contact.region_id) : contact.target;
                row.region_kind   = marked ? std::string() : std::string("hole");
                row.area          = contact.area;
                row.layers        = contact.layers;
                row.z             = contact.at[2];
                rows.push_back(std::move(row));
            }
            if (rows.empty()) {
                CheckRow row;
                row.kind  = CheckRowKind::SupportClear;
                row.count = report.supports->layers;
                rows.push_back(std::move(row));
            }
            CheckSection section  = make_section(PrintCheck::Supports, std::move(rows), report.supports->truncated);
            section.support_layers  = report.supports->layers;
            section.support_objects = report.supports->objects;
            sections.push_back(std::move(section));
        } else {
            clean(PrintCheck::Supports);
        }
    }

    if (report.islands) {
        std::vector<CheckRow> rows;
        for (const Workspace::SliceIsland& island : report.islands->items) {
            if (island.supported)
                continue;
            CheckRow row;
            row.kind      = CheckRowKind::UnsupportedIsland;
            row.severity  = CheckSeverity::Warning;
            row.object    = island.object_name;
            row.object_id = island.object;
            row.area      = island.area;
            row.z         = island.z;
            rows.push_back(std::move(row));
        }
        if (rows.empty())
            clean(PrintCheck::Islands);
        else
            sections.push_back(make_section(PrintCheck::Islands, std::move(rows), report.islands->truncated));
    }

    if (report.seams) {
        std::vector<CheckRow> rows;
        for (const Workspace::SeamPlacement& placement : report.seams->regions) {
            if (placement.seams == 0 || !seams_unwanted_on(placement.kind))
                continue;
            CheckRow row;
            row.kind         = CheckRowKind::SeamsOnRegion;
            row.severity     = CheckSeverity::Warning;
            row.object       = placement.object_name;
            row.region_label = region_label(regions, placement.region_id);
            row.region_kind  = placement.kind;
            row.count        = placement.seams;
            rows.push_back(std::move(row));
        }
        if (rows.empty())
            clean(PrintCheck::Seams);
        else
            sections.push_back(make_section(PrintCheck::Seams, std::move(rows)));
    }

    if (report.first_layer && !report.first_layer->objects.empty()) {
        std::vector<CheckRow> rows;
        for (const Workspace::FirstLayerObject& object : report.first_layer->objects) {
            CheckRow row;
            row.kind      = CheckRowKind::FirstLayerContact;
            row.object    = object.object_name;
            row.object_id = object.object;
            row.area      = object.contact_area;
            row.brim      = object.brim;
            row.z         = report.first_layer->height;
            rows.push_back(std::move(row));
        }
        sections.push_back(make_section(PrintCheck::FirstLayer, std::move(rows)));
    }

    order_check_sections(sections, view.supports_generated);
    view.sections = std::move(sections);

    // The closing line names the clean checks in one fixed order, whatever
    // order they were found in.
    std::vector<PrintCheck> ordered;
    for (PrintCheck check : closing_line_order())
        if (std::find(view.clean.begin(), view.clean.end(), check) != view.clean.end())
            ordered.push_back(check);
    view.clean = std::move(ordered);
    return view;
}

} // namespace Slic3r::GUI::JusPrin
