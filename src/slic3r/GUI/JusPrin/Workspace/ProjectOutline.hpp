#pragma once

#include "Workspace.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

// Questions the Plates pane asks of a ProjectOutline. They are pure functions
// of the outline, so a folded plate's summary can never disagree with the rows
// listed under the plate: both are groupings of the same copies.
namespace Slic3r::GUI::JusPrin::Workspace {

// One copy as a row needs it: the object it belongs to and whether it will print.
struct PlacedCopy
{
    const OutlineObject* object{nullptr};
    const OutlineCopy*   copy{nullptr};

    // A copy prints only when both its own switch and its object's are on.
    bool prints() const { return object->printable && copy->printable; }
};

inline std::vector<PlacedCopy> copies_on_plate(const ProjectOutline& outline, PlateId plate)
{
    std::vector<PlacedCopy> result;
    for (const OutlineObject& object : outline.objects)
        for (const OutlineCopy& copy : object.copies)
            if (copy.plate && *copy.plate == plate)
                result.push_back({&object, &copy});
    return result;
}

// Copies no plate holds. A copy is off the plate only when Orca's plate list
// says so; where it sits on the bed does not decide it.
inline std::vector<PlacedCopy> copies_off_plate(const ProjectOutline& outline)
{
    std::vector<PlacedCopy> result;
    for (const OutlineObject& object : outline.objects)
        for (const OutlineCopy& copy : object.copies)
            if (!copy.plate)
                result.push_back({&object, &copy});
    return result;
}

// What a folded plate says about what it holds, computed from the current
// outline each time. Disabled copies count toward `copies` and are named again
// in `wont_print`, so "3 copies" and "1 won't print" read together.
struct PlateSummary
{
    std::size_t objects{0};      // distinct objects with a copy on the plate
    std::size_t copies{0};       // every copy on the plate, disabled ones included
    std::size_t wont_print{0};   // copies that will not print
    // The one object's name when `objects == 1`; empty otherwise.
    std::string single_object;

    bool empty() const { return copies == 0; }
    friend bool operator==(const PlateSummary& lhs, const PlateSummary& rhs)
    {
        return lhs.objects == rhs.objects && lhs.copies == rhs.copies && lhs.wont_print == rhs.wont_print &&
               lhs.single_object == rhs.single_object;
    }
};

inline PlateSummary summarize_plate(const ProjectOutline& outline, PlateId plate)
{
    PlateSummary summary;
    const OutlineObject* first = nullptr;
    const OutlineObject* last  = nullptr;
    for (const PlacedCopy& placed : copies_on_plate(outline, plate)) {
        ++summary.copies;
        if (!placed.prints())
            ++summary.wont_print;
        if (placed.object != last) {
            // Copies of one object are adjacent, so a change of object is a
            // new object unless an earlier one repeats (it cannot).
            ++summary.objects;
            last = placed.object;
            if (first == nullptr)
                first = placed.object;
        }
    }
    if (summary.objects == 1)
        summary.single_object = first->name;
    return summary;
}

// Whether the pane gives an object volume children. Volumes appear only when
// there is something besides the single model part; a simple object stays one row.
inline bool has_volume_children(const OutlineObject& object)
{
    return object.volumes.size() > 1 ||
           std::any_of(object.volumes.begin(), object.volumes.end(), [](const OutlineVolume& volume) { return volume.role != VolumeRole::Part; });
}

// An object has a filament to show when the job is multi-material or the
// object is assigned to a slot other than the first. Orca gives every object
// the first slot when it is loaded, so that assignment is not news by itself.
inline bool shows_filament(const ProjectOutline& outline, const OutlineObject& object)
{
    return outline.filament_count > 1 || (object.extruder.has_value() && *object.extruder != 1);
}

// The object's copies that sit on a given plate, for the rows under an
// expanded plate. An object appears on a plate if any copy does.
inline std::vector<const OutlineCopy*> object_copies_on_plate(const OutlineObject& object, PlateId plate)
{
    std::vector<const OutlineCopy*> result;
    for (const OutlineCopy& copy : object.copies)
        if (copy.plate && *copy.plate == plate)
            result.push_back(&copy);
    return result;
}

} // namespace Slic3r::GUI::JusPrin::Workspace
