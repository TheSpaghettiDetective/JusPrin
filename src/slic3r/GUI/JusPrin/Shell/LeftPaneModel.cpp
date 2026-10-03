#include "LeftPaneModel.hpp"

#include <algorithm>

namespace Slic3r::GUI::JusPrin {

using namespace Workspace;

namespace {

bool contains(const std::vector<ObjectId>& ids, ObjectId id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// One object's contribution to a plate (or to the off-plate group): the copies
// it has there. Everything about the object itself is shared.
struct Placement
{
    const OutlineObject*            object{nullptr};
    std::vector<const OutlineCopy*> copies;
};

bool selection_is_below(const ProjectOutline& outline, const OutlineObject& object)
{
    const auto selected_copy = [&](const OutlineCopy& copy) {
        return std::find(outline.selected_copies.begin(), outline.selected_copies.end(), copy.id) != outline.selected_copies.end();
    };
    const auto selected_volume = [&](const OutlineVolume& volume) {
        return std::find(outline.selected_volumes.begin(), outline.selected_volumes.end(), volume.id) != outline.selected_volumes.end();
    };
    return std::any_of(object.copies.begin(), object.copies.end(), selected_copy) ||
           std::any_of(object.volumes.begin(), object.volumes.end(), selected_volume);
}

void append_object(std::vector<PaneRow>& rows, const ProjectOutline& outline, const WorkspaceSnapshot& snapshot,
                   const PaneNavigation& navigation, const Placement& placement, PlateId plate, int depth)
{
    const OutlineObject& object = *placement.object;
    const bool           show_copies  = placement.copies.size() > 1;
    const bool           show_volumes = has_volume_children(object);

    PaneRow row;
    row.kind       = PaneRow::Kind::Object;
    row.depth      = depth;
    row.plate      = plate;
    row.object     = object.id;
    row.name       = object.name;
    row.copies     = placement.copies.size();
    row.object_printable = object.printable;
    for (const OutlineCopy* copy : placement.copies)
        if (!object.printable || !copy->printable)
            ++row.wont_print;
    row.selected   = snapshot.selection_status == SelectionStatus::Objects && contains(snapshot.selected_objects, object.id);
    row.expandable = show_copies || show_volumes;
    // An open selection below the object has to be visible, so it opens the row.
    row.expanded   = row.expandable && (navigation.is_expanded(object.id) || selection_is_below(outline, object));
    row.customized    = object.customization.any();
    row.customization = object.customization;
    row.mesh       = object.mesh;
    if (shows_filament(outline, object)) {
        row.filament = object.extruder.value_or(1);
        if (*row.filament >= 1 && std::size_t(*row.filament) <= outline.filament_colours.size())
            row.filament_colour = outline.filament_colours[std::size_t(*row.filament) - 1];
    }
    rows.push_back(row);

    if (!row.expanded)
        return;

    if (show_copies) {
        std::size_t ordinal = 0;
        for (const OutlineCopy* copy : placement.copies) {
            PaneRow child;
            child.kind            = PaneRow::Kind::Copy;
            child.depth           = depth + 1;
            child.plate           = plate;
            child.object          = object.id;
            child.copy            = copy->id;
            child.name            = object.name;
            child.ordinal         = ++ordinal;
            child.copy_wont_print = !object.printable || !copy->printable;
            child.copy_printable = copy->printable;
            child.selected        = std::find(outline.selected_copies.begin(), outline.selected_copies.end(), copy->id) !=
                                    outline.selected_copies.end();
            rows.push_back(child);
        }
    }
    if (show_volumes) {
        for (const OutlineVolume& volume : object.volumes) {
            PaneRow child;
            child.kind     = PaneRow::Kind::Volume;
            child.depth    = depth + 1;
            child.plate    = plate;
            child.object   = object.id;
            child.volume   = volume.id;
            child.name     = volume.name;
            child.role     = volume.role;
            child.mesh     = volume.mesh;
            if ((volume.role == VolumeRole::Part && outline.filament_count > 1) || volume.extruder) {
                child.filament = volume.extruder.value_or(object.extruder.value_or(1));
                if (*child.filament >= 1 && std::size_t(*child.filament) <= outline.filament_colours.size())
                    child.filament_colour = outline.filament_colours[std::size_t(*child.filament) - 1];
            }
            child.selected = std::find(outline.selected_volumes.begin(), outline.selected_volumes.end(), volume.id) !=
                             outline.selected_volumes.end();
            rows.push_back(child);
        }
    }
}

} // namespace

std::vector<PaneRow> build_plate_rows(const ProjectOutline& outline, const WorkspaceSnapshot& snapshot,
                                      const PaneNavigation& navigation)
{
    std::vector<PaneRow> rows;
    for (const OutlinePlate& plate : outline.plates) {
        PaneRow row;
        row.kind            = PaneRow::Kind::Plate;
        row.plate           = plate.id;
        row.name            = plate.name;
        row.selected        = plate.active;
        row.expanded        = plate.active;
        row.sliced          = plate.sliced;
        row.locked          = plate.locked;
        row.custom_settings = plate.custom_settings;
        row.summary         = summarize_plate(outline, plate.id);
        rows.push_back(row);

        if (!plate.active)
            continue;
        for (const OutlineObject& object : outline.objects) {
            Placement placement{&object, object_copies_on_plate(object, plate.id)};
            if (!placement.copies.empty())
                append_object(rows, outline, snapshot, navigation, placement, plate.id, 1);
        }
    }

    // Membership comes from the plate list: a copy shows here only when no
    // plate holds it, so the group is absent for an ordinary project.
    std::vector<Placement> loose;
    for (const OutlineObject& object : outline.objects) {
        Placement placement{&object, {}};
        for (const OutlineCopy& copy : object.copies)
            if (!copy.plate)
                placement.copies.push_back(&copy);
        if (!placement.copies.empty())
            loose.push_back(std::move(placement));
    }
    if (!loose.empty()) {
        PaneRow header;
        header.kind = PaneRow::Kind::OffPlateHeader;
        for (const Placement& placement : loose)
            header.copies += placement.copies.size();
        rows.push_back(header);
        for (const Placement& placement : loose)
            append_object(rows, outline, snapshot, navigation, placement, PlateId{}, 1);
    }
    return rows;
}

} // namespace Slic3r::GUI::JusPrin
