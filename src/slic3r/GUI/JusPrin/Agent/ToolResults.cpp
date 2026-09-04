#include "ToolResults.hpp"

namespace Slic3r::GUI::JusPrin::Agent {
namespace {
using nlohmann::json;
std::string label(const std::string& text, bool& truncated)
{
    if (text.size() <= kToolLabelLimit) return text;
    truncated = true;
    std::size_t end = kToolLabelLimit;
    // Never split a UTF-8 code point.
    while (end && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
    return text.substr(0, end);
}
}

json workspace_inspection(const Workspace::WorkspaceSnapshot& snapshot)
{
    bool truncated = false;
    json plates = json::array(), selected = json::array();
    std::size_t object_count = 0, returned_objects = 0;
    for (const auto& plate : snapshot.plates) {
        object_count += plate.objects.size();
        if (plates.size() == 16) { truncated = true; continue; }
        bool objects_truncated = false;
        json objects = json::array();
        for (const auto& object : plate.objects) {
            if (returned_objects == kToolListLimit) { objects_truncated = true; break; }
            ++returned_objects;
            objects.push_back({{"objectId", std::to_string(object.id.value())},
                               {"name", label(object.name, objects_truncated)}, {"instanceCount", object.instances.size()}});
        }
        plates.push_back({{"plateId", std::to_string(plate.id.value())}, {"name", label(plate.name, truncated)},
                          {"active", plate.active}, {"sliced", plate.sliced}, {"objectCount", plate.objects.size()},
                          {"objects", {{"items", std::move(objects)}, {"truncated", objects_truncated}}}});
        truncated = truncated || objects_truncated;
    }
    for (auto id : snapshot.selected_objects) {
        if (selected.size() == kToolListLimit) break;
        selected.push_back(std::to_string(id.value()));
    }
    const bool selection_truncated = snapshot.selected_objects.size() > selected.size();
    const char* status = snapshot.selection_status == Workspace::SelectionStatus::Objects ? "objects" :
                         snapshot.selection_status == Workspace::SelectionStatus::Unsupported ? "unsupported" : "none";
    json result{{"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision},
                 {"projectName", label(snapshot.setup.project_name, truncated)}, {"projectDirty", snapshot.setup.project_dirty},
                 {"printerPreset", label(snapshot.setup.printer_preset, truncated)},
                 {"filamentPreset", label(snapshot.setup.filament_preset, truncated)},
                 {"activePlateId", snapshot.active_plate ? std::to_string(snapshot.active_plate->value()) : ""},
                 {"plateCount", snapshot.plates.size()}, {"objectCount", object_count},
                 {"plates", {{"items", std::move(plates)}, {"truncated", snapshot.plates.size() > 16}}},
                 {"selection", {{"status", status}, {"items", std::move(selected)}, {"truncated", selection_truncated}}},
                 {"history", {{"canUndo", snapshot.can_undo}, {"canRedo", snapshot.can_redo}}}};
    result["truncated"] = truncated || selection_truncated;
    return result;
}

json selection_inspection(const Workspace::WorkspaceSnapshot& snapshot)
{
    json names = json::array();
    bool truncated = false;
    // Preserve the original selection ordering and names contract.
    for (const auto selected : snapshot.selected_objects) {
        if (names.size() == kToolListLimit) { truncated = true; break; }
        for (const auto& plate : snapshot.plates)
            for (const auto& object : plate.objects)
                if (object.id == selected) {
                    if (names.size() == kToolListLimit) truncated = true;
                    else names.push_back(label(object.name, truncated));
                }
    }
    // Preserve the original ordinary selection contract; add the truncation
    // marker only when its previously unbounded result cannot fit.
    json result{{"selection", std::move(names)}, {"revision", snapshot.revision}};
    if (truncated) result["truncated"] = true;
    return result;
}
} // namespace Slic3r::GUI::JusPrin::Agent
