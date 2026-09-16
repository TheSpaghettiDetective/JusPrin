#include "ToolResults.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SettingsSupport.hpp"
#include "slic3r/GUI/JusPrin/Workspace/UtcTime.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace Slic3r::GUI::JusPrin::Agent {
namespace {
using nlohmann::json;
std::string bounded(const std::string& value, std::size_t limit, bool& truncated)
{
    if (value.size() <= limit) return value;
    truncated = true;
    std::size_t end = limit;
    // Never split a UTF-8 code point.
    while (end && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80) --end;
    return value.substr(0, end);
}

std::string label(const std::string& value, bool& truncated) { return bounded(value, kToolLabelLimit, truncated); }
std::string text(const std::string& value, bool& truncated) { return bounded(value, kToolTextLimit, truncated); }
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

json intent_section_result(const std::vector<IntentField>& fields)
{
    bool truncated = false;
    json items = json::array(), open = json::array();
    for (const IntentField& field : fields) {
        if (items.size() == kToolListLimit) { truncated = true; break; }
        items.push_back({{"field", label(field.field, truncated)},
                         {"value", text(field.value, truncated)},
                         {"question", text(field.question, truncated)},
                         {"provenance", provenance_name(field.provenance)},
                         {"updatedAt", field.updated_at}});
        // What the agent asked about and still has no answer for. With field
        // names the agent invents, this list can only come from the agent
        // having said what it asked.
        if (!field.answered() && !field.question.empty() && open.size() < 32)
            open.push_back(label(field.field, truncated));
    }
    return {{"fields", std::move(items)}, {"openQuestions", std::move(open)}, {"truncated", truncated}};
}

json plan_section_result(const PlanRecord& plan)
{
    bool truncated = false;
    json decisions = json::array();
    for (const PlanDecision& decision : plan.decisions) {
        if (decisions.size() == 16) { truncated = true; break; }
        decisions.push_back({{"topic", label(decision.topic, truncated)},
                             {"statement", text(decision.statement, truncated)},
                             {"confidence", label(decision.confidence, truncated)},
                             {"alternative", text(decision.alternative, truncated)}});
    }
    const auto lines = [&truncated](const std::vector<std::string>& values) {
        json items = json::array();
        for (const std::string& value : values) {
            if (items.size() == 16) { truncated = true; break; }
            items.push_back(text(value, truncated));
        }
        return items;
    };
    return {{"headline", text(plan.headline, truncated)}, {"decisions", std::move(decisions)},
            {"assumptions", lines(plan.assumptions)}, {"risks", lines(plan.risks)},
            {"updatedAt", plan.updated_at}, {"truncated", truncated}};
}

namespace {
const Workspace::PrinterDevice* selected_device(const std::vector<Workspace::PrinterDevice>& devices)
{
    const auto found = std::find_if(devices.begin(), devices.end(), [](const auto& device) { return device.selected; });
    return found == devices.end() ? nullptr : &*found;
}
} // namespace

std::string printer_fact_key(const Workspace::ConfiguredPrinter& configured,
                             const std::vector<Workspace::PrinterDevice>& devices)
{
    const Workspace::PrinterDevice* device = selected_device(devices);
    return device != nullptr ? "device:" + device->id : "preset:" + configured.preset;
}

json setup_substitutions_result(const std::vector<Workspace::SetupSubstitution>& substitutions)
{
    bool truncated = false;
    json result = json::array();
    for (const auto& substitution : substitutions)
        if (result.size() < 32)
            result.push_back({{"kind", substitution.kind}, {"from", label(substitution.from, truncated)},
                              {"reason", label(substitution.reason, truncated)}});
    return result;
}

json setup_edits_result(const std::vector<Workspace::UnsavedEdits>& edits)
{
    bool truncated = false;
    json result = json::array();
    for (const auto& edit : edits)
        result.push_back({{"kind", edit.kind}, {"preset", label(edit.preset, truncated)}, {"count", edit.count}});
    return result;
}

json printer_setup_preview_result(const Workspace::PrinterSetupPreview& preview, const json& mismatches,
                                  const Workspace::WorkspaceSnapshot& snapshot)
{
    bool truncated = false;
    json issues = json::array(), filaments = json::array();
    for (const auto& issue : preview.issues)
        if (issues.size() < 32) issues.push_back({{"code", issue.code}, {"message", label(issue.message, truncated)}});
    for (const auto& filament : preview.resulting.filaments)
        if (filaments.size() < 16) filaments.push_back(label(filament.preset, truncated));
    return {{"valid", preview.valid}, {"issues", std::move(issues)},
            {"resulting", {{"printerPreset", label(preview.resulting.preset, truncated)},
                           {"plateType", preview.resulting.plate_type},
                           {"processPreset", label(preview.process_preset, truncated)},
                           {"filamentPresets", std::move(filaments)}}},
            {"substituted", setup_substitutions_result(preview.substitutions)},
            {"unsavedEdits", setup_edits_result(preview.unsaved_edits)},
            {"mismatches", mismatches},
            {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}};
}

json printer_device_result(const Workspace::PrinterDevice& device)
{
    bool truncated = false;
    json entry{{"id", device.id}, {"name", label(device.name, truncated)}, {"model", device.model},
               {"connection", device.connection}, {"activity", device.activity}, {"materials", json::array()}};
    for (const std::string& material : device.materials)
        if (entry["materials"].size() < 16) entry["materials"].push_back(label(material, truncated));
    // Absent, not zero: a nozzle of zero would be a claim the device never made.
    if (device.nozzle_diameter) entry["nozzleDiameter"] = *device.nozzle_diameter;
    if (device.observed_at_ms)
        entry["observedAt"] = Workspace::utc_timestamp(std::chrono::system_clock::time_point(
            std::chrono::milliseconds(*device.observed_at_ms)));
    return entry;
}

json printer_section_result(const Workspace::ConfiguredPrinter& configured,
                            const std::vector<Workspace::PrinterDevice>& devices,
                            const std::vector<Workspace::PrinterFact>& facts)
{
    bool truncated = false;
    json filaments = json::array();
    for (const auto& filament : configured.filaments)
        if (filaments.size() < 16)
            filaments.push_back({{"preset", label(filament.preset, truncated)}, {"material", filament.material}});
    json result{{"configured", {{"preset", label(configured.preset, truncated)}, {"model", configured.model},
                                {"nozzleDiameters", configured.nozzle_diameters}, {"plateType", configured.plate_type},
                                {"filaments", std::move(filaments)}}},
                // No printer Orca talks to reports which plate is on the bed.
                {"plateObservable", false}, {"factKey", printer_fact_key(configured, devices)}};

    json confirmed = json::array(), mismatches = json::array();
    const auto mismatch = [&mismatches](const char* what, const std::string& configured_value,
                                        const std::string& observed_value, const char* source) {
        if (mismatches.size() < 16)
            mismatches.push_back({{"what", what}, {"configured", configured_value}, {"observed", observed_value}, {"source", source}});
    };
    for (const auto& fact : facts) {
        if (confirmed.size() == 16) break;
        confirmed.push_back({{"fact", fact.fact}, {"value", label(fact.value, truncated)},
                             {"confirmedAt", fact.confirmed_at}, {"expiresAt", fact.expires_at}});
        if (fact.fact == "plate" && !configured.plate_type.empty() &&
            Workspace::ascii_lower(fact.value) != Workspace::ascii_lower(configured.plate_type))
            mismatch("plate", configured.plate_type, fact.value, "user_confirmed");
    }
    result["confirmedFacts"] = std::move(confirmed);

    if (const Workspace::PrinterDevice* device = selected_device(devices)) {
        json observed = printer_device_result(*device);
        if (device->progress_percent) observed["progressPercent"] = *device->progress_percent;
        if (!device->job.empty()) observed["job"] = label(device->job, truncated);
        if (device->nozzle_temperature) observed["nozzleTemperature"] = *device->nozzle_temperature;
        if (device->bed_temperature) observed["bedTemperature"] = *device->bed_temperature;
        result["observed"] = std::move(observed);

        if (!configured.model.empty() && !device->model.empty() && configured.model != device->model)
            mismatch("model", configured.model, device->model, "device");
        if (device->nozzle_diameter && !configured.nozzle_diameters.empty() &&
            std::abs(*device->nozzle_diameter - configured.nozzle_diameters.front()) > 1e-3) {
            char configured_text[32], observed_text[32];
            std::snprintf(configured_text, sizeof configured_text, "%g", configured.nozzle_diameters.front());
            std::snprintf(observed_text, sizeof observed_text, "%g", *device->nozzle_diameter);
            mismatch("nozzle", configured_text, observed_text, "device");
        }
        // A loaded material is enough: which slot feeds which filament is the
        // slicer's mapping, decided at print time.
        std::vector<std::string> loaded;
        for (const auto& type : device->material_types)
            if (!type.empty()) loaded.push_back(Workspace::ascii_lower(type));
        if (!loaded.empty())
            for (const auto& filament : configured.filaments)
                if (!filament.material.empty() &&
                    std::find(loaded.begin(), loaded.end(), Workspace::ascii_lower(filament.material)) == loaded.end()) {
                    std::string observed_text;
                    for (const auto& type : device->material_types)
                        if (!type.empty()) observed_text += (observed_text.empty() ? "" : ", ") + type;
                    mismatch("filament", filament.material, observed_text, "device");
                }
    }
    result["mismatches"] = std::move(mismatches);
    result["truncated"]  = truncated;
    return result;
}

json project_section_result(const Workspace::WorkspaceSnapshot& snapshot, const Workspace::ProjectDetails& details)
{
    bool truncated = false;
    json fields = json::object();
    // Only what the file states; every value came from it.
    const auto field = [&](const char* name, const std::string& value) {
        if (!value.empty())
            fields[name] = {{"value", bounded(value, 4096, truncated)}, {"provenance", "project_file"}};
    };
    field("title", details.title);
    field("designer", details.designer);
    field("description", details.description);
    field("license", details.license);
    field("copyright", details.copyright);
    field("origin", details.origin);
    field("profileTitle", details.profile_title);
    field("profileDescription", details.profile_description);
    json attachments = json::array();
    for (const auto& attachment : details.attachments)
        attachments.push_back({{"attachmentId", label(attachment.id, truncated)}, {"folder", attachment.folder},
                               {"bytes", attachment.bytes}});
    return {{"name", label(snapshot.setup.project_name, truncated)}, {"path", snapshot.setup.project_path},
            {"dirty", snapshot.setup.project_dirty}, {"presetsDirty", snapshot.setup.presets_dirty},
            {"details", std::move(fields)},
            {"attachments", {{"items", std::move(attachments)}, {"truncated", details.attachments_truncated}}},
            {"backupCurrent", details.backup_current}, {"truncated", truncated}};
}

json history_section_result(const Workspace::WorkspaceSnapshot& snapshot, const Workspace::WorkspaceHistory& history)
{
    bool truncated = false;
    json steps = json::array();
    for (const auto& step : history.steps)
        steps.push_back({{"stepId", std::to_string(step.id)}, {"label", label(step.label, truncated)}, {"applied", step.applied}});
    return {{"canUndo", snapshot.can_undo}, {"canRedo", snapshot.can_redo}, {"restorable", history.restorable},
            {"steps", {{"items", std::move(steps)}, {"truncated", history.truncated || truncated}}}};
}

json slicing_section_result(const Workspace::WorkspaceSnapshot& snapshot, const std::string& handle)
{
    bool truncated = false;
    json plates = json::array();
    for (const auto& plate : snapshot.plates) {
        if (plates.size() == 16) { truncated = true; break; }
        const char* status = !plate.estimate ? "none" :
            plate.estimate_status == Workspace::EstimateStatus::Current ? "current" :
            plate.estimate_status == Workspace::EstimateStatus::Recomputing ? "recomputing" : "stale";
        plates.push_back({{"plateId", std::to_string(plate.id.value())}, {"name", label(plate.name, truncated)},
                          {"sliced", plate.sliced}, {"estimateStatus", status},
                          {"invalidatedBy", label(plate.invalidated_by, truncated)}});
    }
    json result{{"running", snapshot.slicing.running}, {"plates", std::move(plates)}, {"truncated", truncated}};
    if (snapshot.slicing.plate) result["plateId"] = std::to_string(snapshot.slicing.plate->value());
    if (snapshot.slicing.percent) result["percent"] = *snapshot.slicing.percent;
    if (!handle.empty()) result["handle"] = handle;
    return result;
}

json workspace_inspection(const Workspace::WorkspaceSnapshot& snapshot, InspectSections sections)
{
    bool truncated = false;
    if (!sections.summary)
        return {{"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision},
                {"truncated", truncated}};
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
                          {"active", plate.active}, {"sliced", plate.sliced},
                          {"objectCount", plate.objects.size()},
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

json slice_report_result(const Workspace::SliceReport& report, Workspace::PlateId plate,
                         const Workspace::WorkspaceSnapshot& snapshot, SliceReportSections sections)
{
    json result{{"valid", report.valid}, {"plateId", std::to_string(plate.value())},
                {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}};
    // A plate with no current slice has nothing to report, and saying so is the
    // answer: an empty summary of zeros would read as a print that costs
    // nothing and takes no time.
    if (!report.valid)
        return result;

    if (sections.summary) {
        json filaments = json::array();
        for (const auto& use : report.filaments) {
            if (filaments.size() == 16) break;
            filaments.push_back({{"extruder", use.extruder}, {"lengthMm", use.length_mm}, {"grams", use.grams},
                                 {"cost", use.cost}, {"hasCost", use.has_cost}});
        }
        result["summary"] = {{"printTimeSeconds", report.print_time_seconds},
                             {"prepareTimeSeconds", report.prepare_time_seconds},
                             {"totalGrams", report.total_grams}, {"totalCost", report.total_cost},
                             {"hasCost", report.has_cost}, {"filaments", std::move(filaments)}};
    }

    if (sections.findings) {
        bool truncated = false;
        json items = json::array();
        // Critical first: a report read at a bound should lose the mildest
        // findings, never the ones that stop the print.
        std::vector<const Workspace::SliceFinding*> ordered;
        for (const auto& finding : report.findings) ordered.push_back(&finding);
        std::stable_partition(ordered.begin(), ordered.end(),
                              [](const Workspace::SliceFinding* finding) { return finding->critical; });
        for (const auto* finding : ordered) {
            if (items.size() == 32) { truncated = true; break; }
            items.push_back({{"code", label(finding->code, truncated)}, {"message", text(finding->message, truncated)},
                             {"critical", finding->critical}, {"object", label(finding->object, truncated)}});
        }
        result["findings"] = {{"items", std::move(items)}, {"conflict", text(report.conflict, truncated)},
                              {"toolpathOutsideBed", report.toolpath_outside}, {"truncated", truncated}};
    }

    if (sections.material) {
        double purged = 0.0, tower = 0.0, support = 0.0;
        for (const auto& use : report.filaments) {
            purged += use.flushed_mm3;
            tower += use.tower_mm3;
            support += use.support_mm3;
        }
        result["material"] = {{"filamentChanges", report.filament_changes}, {"extruderChanges", report.extruder_changes},
                              {"purgedMm3", purged}, {"primeTowerMm3", tower}, {"supportMm3", support}};
    }
    return result;
}

json presets_list_result(const Workspace::PresetListResult& presets, const Workspace::WorkspaceSnapshot& snapshot)
{
    bool truncated = presets.truncated;
    json items     = json::array();
    for (const Workspace::PresetEntry& preset : presets.items)
        items.push_back({{"name", label(preset.name, truncated)}, {"label", label(preset.label, truncated)},
                         {"vendor", label(preset.vendor, truncated)}, {"system", preset.system},
                         {"selected", preset.selected}, {"compatible", preset.compatible}});
    return {{"items", std::move(items)}, {"total", presets.total}, {"nextCursor", presets.next_cursor},
            {"truncated", truncated}, {"sessionId", std::to_string(snapshot.session.value())},
            {"revision", snapshot.revision}};
}

} // namespace Slic3r::GUI::JusPrin::Agent
