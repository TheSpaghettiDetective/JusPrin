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

namespace {
json strings(const std::vector<std::string>& values, bool& truncated)
{
    json items = json::array();
    for (const auto& value : values) {
        if (items.size() == kToolListLimit) { truncated = true; break; }
        items.push_back(label(value, truncated));
    }
    return items;
}

json settings_context(const Workspace::WorkspaceSnapshot& snapshot, bool& truncated)
{
    return {{"processPreset", label(snapshot.setup.process_preset, truncated)},
            {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}};
}

json changes_result(const std::vector<Workspace::SettingChange>& changes, bool& truncated)
{
    json items = json::array();
    for (const auto& change : changes) {
        if (items.size() == kToolListLimit) { truncated = true; break; }
        items.push_back({{"key", change.key}, {"before", change.before}, {"after", change.after}});
    }
    return items;
}

json issues_result(const std::vector<Workspace::SettingIssue>& issues, bool& truncated)
{
    json items = json::array();
    for (const auto& issue : issues) {
        if (items.size() == kToolListLimit) { truncated = true; break; }
        auto item = setting_issue_result(issue);
        truncated = truncated || item["truncated"].get<bool>();
        items.push_back(std::move(item));
    }
    return items;
}
}

json setting_issue_result(const Workspace::SettingIssue& issue)
{
    bool truncated = false;
    json result{{"key", label(issue.key, truncated)}, {"code", issue.code}, {"message", label(issue.message, truncated)},
                {"allowed", strings(issue.allowed, truncated)}, {"suggestions", strings(issue.suggestions, truncated)}};
    if (issue.min) result["min"] = *issue.min;
    if (issue.max) result["max"] = *issue.max;
    result["truncated"] = truncated;
    return result;
}

json settings_search_result(const Workspace::SettingsSearchResult& search, const Workspace::WorkspaceSnapshot& snapshot)
{
    bool truncated = search.truncated;
    auto result = settings_context(snapshot, truncated);
    result["items"] = json::array();
    for (const auto& def : search.items) {
        if (result["items"].size() == 25) { truncated = true; break; }
        bool item_truncated = false;
        json item{{"key", def.key}, {"type", def.type}, {"label", label(def.label, item_truncated)},
            {"category", label(def.category, item_truncated)}, {"description", label(def.description, item_truncated)},
            {"unit", label(def.unit, item_truncated)}, {"writable", def.writable},
            {"enumValues", strings(def.enum_values, item_truncated)}, {"enumLabels", strings(def.enum_labels, item_truncated)}};
        if (def.min) item["min"] = *def.min;
        if (def.max) item["max"] = *def.max;
        item["truncated"] = item_truncated;
        truncated = truncated || item_truncated;
        result["items"].push_back(std::move(item));
    }
    result["nextCursor"] = search.next_cursor;
    result["truncated"] = truncated;
    return result;
}

json settings_read_result(const Workspace::SettingsReadResult& read, const Workspace::WorkspaceSnapshot& snapshot)
{
    bool truncated = false;
    auto result = settings_context(snapshot, truncated);
    result["items"] = json::array();
    for (const auto& value : read.items) {
        if (result["items"].size() == 32) { truncated = true; break; }
        // Canonical values must remain round-trippable; only presentation text
        // is shortened. The read count and transport request bounds cap calls.
        result["items"].push_back({{"key", value.key}, {"value", value.value}, {"type", value.definition.type},
            {"label", label(value.definition.label, truncated)}, {"unit", label(value.definition.unit, truncated)},
            {"differsFromPreset", value.differs_from_preset}, {"differsFromSystem", value.differs_from_system},
            {"writable", value.definition.writable}});
    }
    result["unknownKeys"] = issues_result(read.issues, truncated);
    result["truncated"] = truncated;
    return result;
}

json settings_preview_result(const Workspace::SettingsPreview& preview, const Workspace::WorkspaceSnapshot& snapshot)
{
    bool truncated = false;
    auto result = settings_context(snapshot, truncated);
    result["valid"] = preview.valid;
    result["changes"] = changes_result(preview.changes, truncated);
    result["dependencies"] = changes_result(preview.dependencies, truncated);
    result["issues"] = issues_result(preview.issues, truncated);
    result["warnings"] = issues_result(preview.warnings, truncated);
    result["truncated"] = truncated;
    return result;
}

json settings_apply_result(const Workspace::SettingsPreview& applied, const Workspace::WorkspaceSnapshot& snapshot, bool changed)
{
    bool truncated = false;
    auto result = settings_context(snapshot, truncated);
    auto changes = applied.changes;
    changes.insert(changes.end(), applied.dependencies.begin(), applied.dependencies.end());
    result["applied"] = changed;
    result["changes"] = changes_result(changes, truncated);
    result["normalized"] = json::array();
    for (const auto& issue : applied.warnings)
        if (issue.code == "normalized") {
            if (result["normalized"].size() == kToolListLimit) { truncated = true; break; }
            result["normalized"].push_back(issue.key);
        }
    result["processPresetDirty"] = snapshot.setup.process_preset_dirty;
    result["projectUndo"] = false;
    result["truncated"] = truncated;
    return result;
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
