#include "ToolRegistry.hpp"
#include "ToolResults.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <set>
#include <stdexcept>
#include <utility>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

using nlohmann::json;

json object_schema(json properties, json required = json::array())
{
    return json{{"type", "object"},
                {"properties", std::move(properties)},
                {"required", std::move(required)},
                {"additionalProperties", false}};
}

json string_schema() { return json{{"type", "string"}}; }
json number_schema() { return json{{"type", "number"}}; }
json integer_schema() { return json{{"type", "integer"}, {"minimum", 0}}; }
json boolean_schema() { return json{{"type", "boolean"}}; }

json string_array_schema()
{
    return json{{"type", "array"}, {"items", string_schema()}};
}

bool has_only(const json& value, std::initializer_list<std::string_view> allowed)
{
    if (!value.is_object())
        return false;
    for (const auto& item : value.items()) {
        if (std::none_of(allowed.begin(), allowed.end(), [&](std::string_view key) { return item.key() == key; }))
            return false;
    }
    return true;
}

bool is_unsigned_string(const json& value)
{
    if (!value.is_string())
        return false;
    const std::string& text = value.get_ref<const std::string&>();
    if (text.empty())
        return false;
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    return result.ec == std::errc() && result.ptr == text.data() + text.size() && parsed != 0;
}

bool optional_string(const json& arguments, const char* key)
{
    return !arguments.contains(key) || arguments[key].is_string();
}

bool optional_number(const json& arguments, const char* key)
{
    return !arguments.contains(key) || arguments[key].is_number();
}

bool optional_unsigned(const json& arguments, const char* key)
{
    return !arguments.contains(key) || arguments[key].is_number_unsigned();
}

// Prose the agent writes: present or absent, a string either way, and bounded
// so one answer cannot fill a context window.
bool optional_text(const json& arguments, const char* key)
{
    return !arguments.contains(key) ||
           (arguments[key].is_string() && arguments[key].get_ref<const std::string&>().size() <= kToolTextLimit);
}

bool valid_arguments(const ToolDefinition& definition, const json& arguments)
{
    if (!arguments.is_object())
        return false;

    if (definition.handler == ToolHandler::SettingsSearch)
        return has_only(arguments, {"query", "limit", "cursor"}) && arguments.contains("query") &&
               arguments["query"].is_string() && optional_string(arguments, "cursor") &&
               (!arguments.contains("limit") || (arguments["limit"].is_number_unsigned() &&
                 arguments["limit"].get<std::uint64_t>() >= 1 && arguments["limit"].get<std::uint64_t>() <= 25));
    if (definition.handler == ToolHandler::SettingsGet) {
        if (!has_only(arguments, {"keys"}) || !arguments.contains("keys") || !arguments["keys"].is_array() ||
            arguments["keys"].empty() || arguments["keys"].size() > 32)
            return false;
        return std::all_of(arguments["keys"].begin(), arguments["keys"].end(), [](const auto& key) { return key.is_string(); });
    }
    if (definition.handler == ToolHandler::SettingsPreviewPatch || definition.handler == ToolHandler::SettingsApplyPatch) {
        const bool apply = definition.handler == ToolHandler::SettingsApplyPatch;
        if (!(apply ? has_only(arguments, {"changes", "expectedSessionId", "expectedRevision", "intent"}) : has_only(arguments, {"changes"})) ||
            !arguments.contains("changes") || !arguments["changes"].is_object() || arguments["changes"].empty() ||
            arguments["changes"].size() > 32)
            return false;
        if (apply && (!arguments.contains("expectedSessionId") || !is_unsigned_string(arguments["expectedSessionId"]) ||
                      !arguments.contains("expectedRevision") || !arguments["expectedRevision"].is_number_unsigned()))
            return false;
        if (apply && !optional_string(arguments, "intent"))
            return false;
        return std::all_of(arguments["changes"].begin(), arguments["changes"].end(), [](const auto& value) {
            return value.is_string() || value.is_number() || value.is_boolean();
        });
    }

    if (definition.handler == ToolHandler::InspectSelection)
        return arguments.empty();

    if (definition.handler == ToolHandler::WorkspaceInspect) {
        if (!has_only(arguments, {"sections"}))
            return false;
        if (!arguments.contains("sections"))
            return true; // the summary, as every caller before sections existed asked for
        const json& sections = arguments["sections"];
        if (!sections.is_array() || sections.empty() || sections.size() > 7)
            return false;
        std::set<std::string> seen;
        for (const auto& section : sections) {
            if (!section.is_string())
                return false;
            const std::string& name = section.get_ref<const std::string&>();
            if ((name != "summary" && name != "intent" && name != "plan" && name != "slicing" && name != "history" && name != "printer" && name != "project") ||
                !seen.insert(name).second)
                return false;
        }
        return true;
    }

    if (definition.handler == ToolHandler::PrinterList)
        return arguments.empty();

    if (definition.handler == ToolHandler::HistoryRestore)
        return has_only(arguments, {"sessionId", "stepId", "point"}) && arguments.size() == 3 &&
               is_unsigned_string(arguments["sessionId"]) && arguments.contains("stepId") &&
               is_unsigned_string(arguments["stepId"]) && arguments.contains("point") &&
               (arguments["point"] == "before" || arguments["point"] == "after");

    if (definition.handler == ToolHandler::ProjectOpen) {
        if (!has_only(arguments, {"path", "new", "loadProjectSettings", "unitConversion", "oversized", "unsavedWork"}))
            return false;
        const bool by_path = arguments.contains("path");
        const bool fresh   = arguments.contains("new");
        if (by_path == fresh)
            return false; // exactly one
        if (by_path && (!arguments["path"].is_string() || arguments["path"].get_ref<const std::string&>().empty() ||
                        arguments["path"].get_ref<const std::string&>().size() > 1024))
            return false;
        if (fresh && arguments["new"] != true)
            return false;
        const auto one_of = [&arguments](const char* key, std::initializer_list<const char*> allowed) {
            return !arguments.contains(key) ||
                   std::any_of(allowed.begin(), allowed.end(), [&](const char* value) { return arguments[key] == value; });
        };
        return one_of("loadProjectSettings", {"project", "keep"}) && one_of("unitConversion", {"keep", "convertIfTiny", "inches"}) &&
               one_of("oversized", {"keep", "scaleToFit"}) && one_of("unsavedWork", {"discard"}) &&
               (by_path || (!arguments.contains("loadProjectSettings") && !arguments.contains("unitConversion") &&
                            !arguments.contains("oversized")));
    }

    if (definition.handler == ToolHandler::ProjectSave)
        return has_only(arguments, {"path"}) &&
               (!arguments.contains("path") ||
                (arguments["path"].is_string() && !arguments["path"].get_ref<const std::string&>().empty() &&
                 arguments["path"].get_ref<const std::string&>().size() <= 1024));

    if (definition.handler == ToolHandler::PrinterSetupPreview || definition.handler == ToolHandler::PrinterSetup) {
        const bool apply = definition.handler == ToolHandler::PrinterSetup;
        if (!(apply ? has_only(arguments, {"printerPreset", "plateType", "processPreset", "filamentPresets", "unsavedEdits", "confirmFacts"}) :
                      has_only(arguments, {"printerPreset", "plateType", "processPreset", "filamentPresets"})) ||
            arguments.empty())
            return false;
        for (const char* key : {"printerPreset", "plateType", "processPreset"})
            if (arguments.contains(key) && (!arguments[key].is_string() || arguments[key].get_ref<const std::string&>().empty() ||
                                            arguments[key].get_ref<const std::string&>().size() > kToolLabelLimit))
                return false;
        if (arguments.contains("filamentPresets")) {
            const json& names = arguments["filamentPresets"];
            if (!names.is_array() || names.empty() || names.size() > 16 ||
                !std::all_of(names.begin(), names.end(), [](const json& name) {
                    return name.is_string() && !name.get_ref<const std::string&>().empty() &&
                           name.get_ref<const std::string&>().size() <= kToolLabelLimit;
                }))
                return false;
        }
        if (arguments.contains("unsavedEdits") && arguments["unsavedEdits"] != "discard")
            return false;
        if (arguments.contains("confirmFacts")) {
            const json& facts = arguments["confirmFacts"];
            if (!facts.is_array() || facts.empty() || facts.size() > 16)
                return false;
            for (const json& fact : facts)
                if (!has_only(fact, {"fact", "value", "hours"}) || !fact.contains("fact") || !fact.contains("value") ||
                    !fact["fact"].is_string() || fact["fact"].get_ref<const std::string&>().empty() ||
                    fact["fact"].get_ref<const std::string&>().size() > 64 || !fact["value"].is_string() ||
                    fact["value"].get_ref<const std::string&>().empty() || fact["value"].get_ref<const std::string&>().size() > kToolLabelLimit ||
                    (fact.contains("hours") && (!fact["hours"].is_number_unsigned() || fact["hours"].get<std::uint64_t>() < 1 ||
                                                fact["hours"].get<std::uint64_t>() > 720)))
                    return false;
        }
        return true;
    }

    if (definition.handler == ToolHandler::PresetsList) {
        if (!has_only(arguments, {"kind", "query", "compatibleOnly", "limit", "cursor"}) || !arguments.contains("kind") ||
            !arguments["kind"].is_string() || !optional_string(arguments, "query") || !optional_string(arguments, "cursor") ||
            (arguments.contains("compatibleOnly") && !arguments["compatibleOnly"].is_boolean()))
            return false;
        const std::string& kind = arguments["kind"].get_ref<const std::string&>();
        if (kind != "printer" && kind != "filament" && kind != "process")
            return false;
        return !arguments.contains("limit") ||
               (arguments["limit"].is_number_unsigned() && arguments["limit"].get<std::uint64_t>() >= 1 &&
                arguments["limit"].get<std::uint64_t>() <= 25);
    }

    if (definition.handler == ToolHandler::SliceReportRead) {
        if (!has_only(arguments, {"plateId", "sections"}) ||
            (arguments.contains("plateId") && !is_unsigned_string(arguments["plateId"])))
            return false;
        if (!arguments.contains("sections"))
            return true;
        const json& sections = arguments["sections"];
        if (!sections.is_array() || sections.empty() || sections.size() > 3)
            return false;
        std::set<std::string> seen;
        for (const auto& section : sections) {
            if (!section.is_string())
                return false;
            const std::string& name = section.get_ref<const std::string&>();
            if ((name != "summary" && name != "findings" && name != "material") || !seen.insert(name).second)
                return false;
        }
        return true;
    }

    if (definition.handler == ToolHandler::SliceStart)
        return has_only(arguments, {"plateId", "preempt"}) &&
               (!arguments.contains("plateId") || is_unsigned_string(arguments["plateId"])) &&
               (!arguments.contains("preempt") || arguments["preempt"].is_boolean());

    if (definition.handler == ToolHandler::IntentUpdate) {
        if (!has_only(arguments, {"fields"}) || !arguments.contains("fields") || !arguments["fields"].is_array() ||
            arguments["fields"].empty() || arguments["fields"].size() > 32)
            return false;
        return std::all_of(arguments["fields"].begin(), arguments["fields"].end(), [](const json& field) {
            if (!has_only(field, {"field", "value", "question", "assumed"}) || !field.contains("field") ||
                !field["field"].is_string() || field["field"].get_ref<const std::string&>().empty() ||
                !optional_text(field, "value") || !optional_text(field, "question") ||
                (field.contains("assumed") && !field["assumed"].is_boolean()))
                return false;
            // An entry that neither answers nor asks records nothing.
            return !field.value("value", std::string()).empty() || !field.value("question", std::string()).empty();
        });
    }

    if (definition.handler == ToolHandler::PlanSet) {
        if (!has_only(arguments, {"headline", "decisions", "assumptions", "risks"}) || !arguments.contains("headline") ||
            !optional_text(arguments, "headline") || arguments["headline"].get_ref<const std::string&>().empty())
            return false;
        for (const char* key : {"assumptions", "risks"}) {
            if (!arguments.contains(key))
                continue;
            const json& lines = arguments[key];
            if (!lines.is_array() || lines.size() > 16 ||
                !std::all_of(lines.begin(), lines.end(), [](const json& line) {
                    return line.is_string() && line.get_ref<const std::string&>().size() <= kToolTextLimit;
                }))
                return false;
        }
        if (!arguments.contains("decisions"))
            return true;
        const json& decisions = arguments["decisions"];
        if (!decisions.is_array() || decisions.size() > 16)
            return false;
        return std::all_of(decisions.begin(), decisions.end(), [](const json& decision) {
            return has_only(decision, {"topic", "statement", "confidence", "alternative"}) && decision.contains("topic") &&
                   decision.contains("statement") && optional_text(decision, "topic") &&
                   optional_text(decision, "statement") && optional_text(decision, "confidence") &&
                   optional_text(decision, "alternative") && !decision["topic"].get_ref<const std::string&>().empty() &&
                   !decision["statement"].get_ref<const std::string&>().empty();
        });
    }

    if (definition.handler == ToolHandler::DuplicateObject)
        return has_only(arguments, {"sessionId", "objectId"}) && arguments.size() == 2 &&
               arguments.contains("sessionId") && is_unsigned_string(arguments["sessionId"]) &&
               arguments.contains("objectId") && is_unsigned_string(arguments["objectId"]);

    if (definition.handler == ToolHandler::ImportModel)
        return has_only(arguments, {"sessionId", "attachmentId"}) && arguments.size() == 2 &&
               arguments.contains("sessionId") && is_unsigned_string(arguments["sessionId"]) &&
               arguments.contains("attachmentId") && arguments["attachmentId"].is_string() &&
               !arguments["attachmentId"].get_ref<const std::string&>().empty();

    if (definition.handler == ToolHandler::RecordBuild) {
        if (!has_only(arguments, {"slicerVersion", "configurationProvenance", "printTimeSeconds", "filamentMm",
                                  "materialGrams", "materialCost", "layerCount", "warnings"}) ||
            !optional_string(arguments, "slicerVersion") || !optional_string(arguments, "configurationProvenance") ||
            !optional_number(arguments, "printTimeSeconds") || !optional_number(arguments, "filamentMm") ||
            !optional_number(arguments, "materialGrams") || !optional_number(arguments, "materialCost") ||
            !optional_unsigned(arguments, "layerCount"))
            return false;
        if (arguments.contains("warnings")) {
            if (!arguments["warnings"].is_array())
                return false;
            for (const json& warning : arguments["warnings"])
                if (!warning.is_string())
                    return false;
        }
        return true;
    }

    if (definition.handler == ToolHandler::RecordExportCopy)
        return has_only(arguments, {"buildId", "destination", "observedOutputHash"}) &&
               optional_string(arguments, "buildId") && optional_string(arguments, "destination") &&
               optional_string(arguments, "observedOutputHash");

    if (definition.handler == ToolHandler::RecordPhysicalPrint)
        return has_only(arguments, {"buildId", "printer", "material", "startedAt", "endedAt", "outcome",
                                    "failure", "gcodeHash"}) &&
               optional_string(arguments, "buildId") && optional_string(arguments, "printer") &&
               optional_string(arguments, "material") && optional_string(arguments, "startedAt") &&
               optional_string(arguments, "endedAt") && optional_string(arguments, "outcome") &&
               optional_string(arguments, "failure") && optional_string(arguments, "gcodeHash");

    return false;
}

std::vector<ToolDefinition> make_definitions()
{
    const json revision = integer_schema();
    const json id       = string_schema();
    const auto list_schema = [](json item) {
        return object_schema({{"items", {{"type", "array"}, {"items", std::move(item)}, {"maxItems", kToolListLimit}}},
                               {"truncated", boolean_schema()}}, {"items", "truncated"});
    };
    const json device_summary = object_schema(
        {{"id", id}, {"name", string_schema()}, {"model", string_schema()}, {"connection", string_schema()},
         {"activity", {{"type", "string"}, {"enum", json::array({"offline", "idle", "printing"})}}},
         {"materials", {{"type", "array"}, {"items", string_schema()}, {"maxItems", 16}}},
         {"nozzleDiameter", number_schema()}, {"observedAt", string_schema()}, {"progressPercent", integer_schema()},
         {"job", string_schema()}, {"nozzleTemperature", number_schema()}, {"bedTemperature", number_schema()}},
        {"id", "name", "model", "connection", "activity", "materials"});
    const json printer_section = object_schema(
        {{"configured", object_schema({{"preset", string_schema()}, {"model", string_schema()},
                                       {"nozzleDiameters", {{"type", "array"}, {"items", number_schema()}}},
                                       {"plateType", string_schema()},
                                       {"filaments", {{"type", "array"}, {"maxItems", 16},
                                                      {"items", object_schema({{"preset", string_schema()}, {"material", string_schema()}},
                                                                              {"preset", "material"})}}}},
                                      {"preset", "model", "nozzleDiameters", "plateType", "filaments"})},
         {"observed", device_summary}, {"plateObservable", boolean_schema()}, {"factKey", string_schema()},
         {"confirmedFacts", {{"type", "array"}, {"maxItems", 16},
                             {"items", object_schema({{"fact", string_schema()}, {"value", string_schema()},
                                                      {"confirmedAt", string_schema()}, {"expiresAt", string_schema()}},
                                                     {"fact", "value", "confirmedAt", "expiresAt"})}}},
         {"mismatches", {{"type", "array"}, {"maxItems", 16},
                         {"items", object_schema({{"what", {{"type", "string"}, {"enum", json::array({"model", "nozzle", "filament", "plate"})}}},
                                                  {"configured", string_schema()}, {"observed", string_schema()},
                                                  {"source", {{"type", "string"}, {"enum", json::array({"device", "user_confirmed"})}}}},
                                                 {"what", "configured", "observed", "source"})}}},
         {"truncated", boolean_schema()}},
        {"configured", "plateObservable", "factKey", "confirmedFacts", "mismatches", "truncated"});
    const json sourced_text = object_schema({{"value", string_schema()}, {"provenance", {{"type", "string"}, {"enum", json::array({"project_file"})}}}},
                                            {"value", "provenance"});
    json details_schema = object_schema({});
    for (const char* name : {"title", "designer", "description", "license", "copyright", "origin", "profileTitle", "profileDescription"})
        details_schema["properties"][name] = sourced_text;
    const json project_section = object_schema(
        {{"name", string_schema()}, {"path", string_schema()}, {"dirty", boolean_schema()}, {"presetsDirty", boolean_schema()},
         {"details", details_schema},
         {"attachments", list_schema(object_schema({{"attachmentId", id}, {"folder", string_schema()}, {"bytes", integer_schema()}},
                                                   {"attachmentId", "folder", "bytes"}))},
         {"backupCurrent", boolean_schema()}, {"truncated", boolean_schema()}},
        {"name", "path", "dirty", "presetsDirty", "details", "attachments", "backupCurrent", "truncated"});
    const json setup_changes = {{"type", "array"}, {"maxItems", 32},
                                {"items", object_schema({{"kind", {{"type", "string"}, {"enum", json::array({"printer", "plate", "process", "filament"})}}},
                                                         {"from", string_schema()}, {"reason", string_schema()}},
                                                        {"kind", "from", "reason"})}};
    const json setup_edits = {{"type", "array"}, {"maxItems", 3},
                              {"items", object_schema({{"kind", string_schema()}, {"preset", string_schema()}, {"count", integer_schema()}},
                                                      {"kind", "preset", "count"})}};
    // The summary's two flags, plus the steps when the history section is asked for.
    const json history_section = object_schema(
        {{"canUndo", boolean_schema()}, {"canRedo", boolean_schema()}, {"restorable", boolean_schema()},
         {"steps", list_schema(object_schema({{"stepId", id}, {"label", string_schema()}, {"applied", boolean_schema()}},
                                             {"stepId", "label", "applied"}))}},
        {"canUndo", "canRedo"});
    const json object_summary = object_schema({{"objectId", id}, {"name", string_schema()}, {"instanceCount", revision}},
                                               {"objectId", "name", "instanceCount"});
    const json plate_summary = object_schema({{"plateId", id}, {"name", string_schema()}, {"active", boolean_schema()},
                                              {"sliced", boolean_schema()}, {"objectCount", revision},
                                              {"objects", list_schema(object_summary)}},
                                              {"plateId", "name", "active", "sliced", "objectCount", "objects"});
    json selection_summary = list_schema(id);
    selection_summary["properties"]["status"] = string_schema();
    selection_summary["required"].push_back("status");

    const auto array_schema = [](json item, std::size_t limit = kToolListLimit) {
        return json{{"type", "array"}, {"items", std::move(item)}, {"maxItems", limit}};
    };
    const auto settings_output = [&](json fields, json required) {
        fields["processPreset"] = string_schema();
        fields["sessionId"] = id;
        fields["revision"] = revision;
        fields["truncated"] = boolean_schema();
        for (const auto* key : {"processPreset", "sessionId", "revision", "truncated"}) required.push_back(key);
        return object_schema(std::move(fields), std::move(required));
    };
    const json setting_def = object_schema({{"key", id}, {"type", id}, {"label", id}, {"category", id},
        {"description", id}, {"unit", id}, {"min", number_schema()}, {"max", number_schema()},
        {"enumValues", array_schema(id)}, {"enumLabels", array_schema(id)}, {"writable", boolean_schema()},
        {"truncated", boolean_schema()}},
        {"key", "type", "label", "category", "description", "unit", "enumValues", "enumLabels", "writable", "truncated"});
    const json issue = object_schema({{"key", id}, {"code", id}, {"message", id}, {"allowed", array_schema(id)},
        {"suggestions", array_schema(id)}, {"min", number_schema()}, {"max", number_schema()}, {"truncated", boolean_schema()}},
        {"key", "code", "message", "allowed", "suggestions", "truncated"});
    const json change = object_schema({{"key", id}, {"before", id}, {"after", id}}, {"key", "before", "after"});
    const json changes_input{{"type", "object"}, {"minProperties", 1}, {"maxProperties", 32},
        {"additionalProperties", {{"type", json::array({"string", "number", "boolean"})}}}};
    const json patch_output = settings_output({{"valid", boolean_schema()}, {"changes", array_schema(change)},
        {"dependencies", array_schema(change)}, {"issues", array_schema(issue)}, {"warnings", array_schema(issue)}},
        {"valid", "changes", "dependencies", "issues", "warnings"});

    // Product state: what the user wants from this print, and the plan the
    // agent means to follow. Field names are the agent's own; the fixed part
    // is where an answer came from.
    const json provenance{{"type", "string"},
                          {"enum", json::array({"file", "observed", "agent_inferred", "user_confirmed"})}};
    const json text{{"type", "string"}, {"maxLength", kToolTextLimit}};
    const json intent_field = object_schema({{"field", id}, {"value", text}, {"question", text},
                                             {"provenance", provenance}, {"updatedAt", id}},
                                            {"field", "value", "question", "provenance", "updatedAt"});
    const json intent_section = object_schema({{"fields", array_schema(intent_field, 64)},
                                               {"openQuestions", array_schema(id, 32)},
                                               {"truncated", boolean_schema()}},
                                              {"fields", "openQuestions", "truncated"});
    const json plan_decision = object_schema({{"topic", id}, {"statement", text}, {"confidence", id},
                                              {"alternative", text}},
                                             {"topic", "statement", "confidence", "alternative"});
    const json plan_section = object_schema({{"headline", text}, {"decisions", array_schema(plan_decision, 16)},
                                             {"assumptions", array_schema(text, 16)}, {"risks", array_schema(text, 16)},
                                             {"updatedAt", id}, {"truncated", boolean_schema()}},
                                            {"headline", "decisions", "assumptions", "risks", "updatedAt", "truncated"});
    const json intent_output = object_schema({{"intent", intent_section}, {"sessionId", id}, {"revision", revision},
                                              {"projectUndo", boolean_schema()}},
                                             {"intent", "sessionId", "revision", "projectUndo"});
    const json plan_output = object_schema({{"plan", plan_section}, {"sessionId", id}, {"revision", revision},
                                            {"projectUndo", boolean_schema()}},
                                           {"plan", "sessionId", "revision", "projectUndo"});

    // What the slicer is doing, and what each plate currently holds. One
    // background process serves the whole application, so "running" is not a
    // per-plate fact and is not reported as one.
    const json slicing_plate = object_schema({{"plateId", id}, {"name", string_schema()}, {"sliced", boolean_schema()},
                                              {"estimateStatus", {{"type", "string"},
                                                                  {"enum", json::array({"current", "recomputing", "stale", "none"})}}},
                                              {"invalidatedBy", string_schema()}},
                                             {"plateId", "name", "sliced", "estimateStatus", "invalidatedBy"});
    const json slicing_section = object_schema({{"running", boolean_schema()}, {"plateId", id},
                                                {"percent", integer_schema()}, {"handle", id},
                                                {"plates", array_schema(slicing_plate, 16)},
                                                {"truncated", boolean_schema()}},
                                               {"running", "plates", "truncated"});

    std::vector<ToolDefinition> definitions{
        {"settings_search", "Search process settings",
         "Find a page of process settings by key, label, or description. Requires an active FFF process preset. A page is not the full writable list; read known keys directly with settings_get or follow nextCursor.",
         object_schema({{"query", id}, {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 25}}}, {"cursor", id}}, {"query"}),
         settings_output({{"items", array_schema(setting_def, 25)}, {"nextCursor", id}}, {"items", "nextCursor"}),
         ActionClass::ReadOnly, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::SettingsSearch},
        {"settings_get", "Read process settings",
         "Read current process values and their preset origin. Requires an active FFF process preset; read before proposing a patch.",
         object_schema({{"keys", {{"type", "array"}, {"items", id}, {"minItems", 1}, {"maxItems", 32}}}}, {"keys"}),
         settings_output({{"items", array_schema(object_schema({{"key", id}, {"value", id}, {"type", id}, {"label", id},
             {"unit", id}, {"differsFromPreset", boolean_schema()}, {"differsFromSystem", boolean_schema()}, {"writable", boolean_schema()}},
             {"key", "value", "type", "label", "unit", "differsFromPreset", "differsFromSystem", "writable"}), 32)},
             {"unknownKeys", array_schema(issue, 32)}}, {"items", "unknownKeys"}),
         ActionClass::ReadOnly, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::SettingsGet},
        {"settings_preview_patch", "Preview process settings",
         "Validate an atomic process-settings patch without changing the workspace. Requires an active FFF process preset; use the returned sessionId and revision when applying.",
         object_schema({{"changes", changes_input}}, {"changes"}), patch_output,
         ActionClass::ReadOnly, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::SettingsPreviewPatch},
        {"settings_apply_patch", "Change process settings",
         "Apply an atomic process-settings patch. Requires an active FFF process preset and the sessionId and revision from a fresh preview. Waits for approval in JusPrin; project Undo does not undo this change.",
         object_schema({{"changes", changes_input}, {"expectedSessionId", id}, {"expectedRevision", revision},
                        {"intent", json{{"type", "string"}, {"maxLength", 40},
                                        {"description", "What the user asked this setup to be, in their own words, as "
                                         "one line of at most 40 characters. Not a description of the settings you "
                                         "changed. Send it whenever the change came from something the user asked for."}}}},
                       {"changes", "expectedSessionId", "expectedRevision"}),
         settings_output({{"applied", boolean_schema()}, {"changes", array_schema(change)}, {"normalized", array_schema(id)},
             {"processPresetDirty", boolean_schema()}, {"projectUndo", boolean_schema()}},
             {"applied", "changes", "normalized", "processPresetDirty", "projectUndo"}),
         ActionClass::Mutation, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::SettingsApplyPatch},
        {"duplicate_object",
         "Duplicate project object",
         "Propose duplicating one existing object in the current project.",
         object_schema(json{{"sessionId", string_schema()}, {"objectId", string_schema()}},
                       json::array({"sessionId", "objectId"})),
         object_schema(json{{"revision", revision}, {"newObjectId", id}}, json::array({"revision"})),
         ActionClass::Mutation,
         ToolExposure::InApp,
         ToolAvailability::Always,
         ToolHandler::DuplicateObject},
        {"import_model",
         "Import attached model",
         "Propose importing one attached model into the current project.",
         object_schema(json{{"sessionId", string_schema()}, {"attachmentId", string_schema()}},
                       json::array({"sessionId", "attachmentId"})),
         object_schema(json{{"revision", revision}, {"imported", boolean_schema()}, {"newObjectId", id}},
                       json::array({"revision", "imported"})),
         ActionClass::Mutation,
         ToolExposure::InApp,
         ToolAvailability::ImportableAttachment,
         ToolHandler::ImportModel},
        {"inspect_selection",
         "Inspect the current selection",
         "Read the current selection without changing the project.",
         object_schema(json::object()),
         object_schema(json{{"selection", string_array_schema()}, {"revision", revision}, {"truncated", boolean_schema()}},
                       json::array({"selection", "revision"})),
         ActionClass::ReadOnly,
         ToolExposure::InApp,
         ToolAvailability::Always,
         ToolHandler::InspectSelection},
        {"intent_update", "Record what this print is for",
         "Record what the user wants out of this print, as named answers you choose: what it is for, how it will be used, what matters about it, how long it may take. Send question without value for something you have asked and do not know yet; the unanswered ones come back as openQuestions. Waits for approval in JusPrin, because the card is where the user confirms you understood them. Project Undo does not undo this.",
         object_schema({{"fields", array_schema(object_schema({{"field", id}, {"value", text}, {"question", text},
                                                               {"assumed", boolean_schema()}}, {"field"}), 32)}},
                       {"fields"}),
         intent_output,
         ActionClass::Mutation, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::IntentUpdate},
        {"plan_set", "Pin your plan for this print",
         "State how you mean to print this project and why: the headline, one entry per decision with the alternative you rejected, what you assumed without being able to check, and what could still go wrong. Replaces the whole plan. Runs without an approval card because it records only your own words; project Undo does not undo this.",
         object_schema({{"headline", text}, {"decisions", array_schema(object_schema({{"topic", id}, {"statement", text},
                                                                                      {"confidence", id}, {"alternative", text}},
                                                                                     {"topic", "statement"}), 16)},
                        {"assumptions", array_schema(text, 16)}, {"risks", array_schema(text, 16)}},
                       {"headline"}),
         plan_output,
         ActionClass::Mutation, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::PlanSet,
         true},
        {"printer_list", "List the printers this app knows",
         "List the physical printers this application knows about: what each is doing, the nozzle and loaded materials it last reported, and when it last said anything (ISO 8601, UTC). A printer it has not heard from is listed as offline, which is a statement about what can be observed rather than a claim about the machine; fields it has not reported are absent rather than zero.",
         object_schema(json::object()),
         object_schema({{"items", array_schema(object_schema({{"id", id}, {"name", string_schema()}, {"model", string_schema()},
                                                              {"connection", string_schema()},
                                                              {"activity", {{"type", "string"},
                                                                            {"enum", json::array({"offline", "idle", "printing"})}}},
                                                              {"nozzleDiameter", number_schema()},
                                                              {"materials", array_schema(id, 16)},
                                                              {"observedAt", id}},
                                                             {"id", "name", "model", "connection", "activity", "materials"}), 25)},
                        {"truncated", boolean_schema()}, {"sessionId", id}, {"revision", revision}},
                       {"items", "truncated", "sessionId", "revision"}),
         ActionClass::ReadOnly, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::PrinterList},
        {"project_open", "Open a project",
         "Replace the open project: open a .3mf project, open a model file (.stl, .obj, .step, .amf) as a new project, or start an empty one with new. OrcaSlicer's questions become inputs: loadProjectSettings (project: use the file's printer, filament and process settings; keep: geometry only), unitConversion (keep; convertIfTiny: scale an object that looks modelled in metres or inches; inches: treat the model file as inches), oversized (keep, or scaleToFit the bed). Anything else OrcaSlicer would ask is answered with the choice that changes least and listed in decisions. If the open project has unsaved changes the call is refused unless unsavedWork is \"discard\", which the user must have agreed to. IDs from before are no longer valid afterwards. Waits for approval in JusPrin, and the card shows the path.",
         object_schema({{"path", {{"type", "string"}, {"maxLength", 1024}}},
                        {"new", {{"type", "boolean"}, {"enum", json::array({true})}}},
                        {"loadProjectSettings", {{"type", "string"}, {"enum", json::array({"project", "keep"})}}},
                        {"unitConversion", {{"type", "string"}, {"enum", json::array({"keep", "convertIfTiny", "inches"})}}},
                        {"oversized", {{"type", "string"}, {"enum", json::array({"keep", "scaleToFit"})}}},
                        {"unsavedWork", {{"type", "string"}, {"enum", json::array({"discard"})}}}}),
         object_schema({{"projectName", string_schema()}, {"path", string_schema()}, {"plateCount", revision},
                        {"objectCount", revision},
                        {"decisions", {{"type", "array"}, {"maxItems", 32},
                                       {"items", object_schema({{"question", string_schema()},
                                                                {"answer", {{"type", "string"}, {"enum", json::array({"yes", "no", "ok", "cancel"})}}}},
                                                               {"question", "answer"})}}},
                        {"sessionId", id}, {"revision", revision}},
                       {"projectName", "path", "plateCount", "objectCount", "decisions", "sessionId", "revision"}),
         ActionClass::Destructive, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::ProjectOpen},
        {"project_save", "Save the project",
         "Save the open project to its own file, or to the absolute .3mf path you give, the way the user's Save does: the file becomes the project's file and the project is marked saved. Replaces whatever is at that path, so it waits for approval in JusPrin, and the card shows the exact path. A project that has never been saved needs a path.",
         object_schema({{"path", {{"type", "string"}, {"maxLength", 1024}}}}),
         object_schema({{"path", string_schema()}, {"saved", boolean_schema()}, {"projectDirty", boolean_schema()},
                        {"sessionId", id}, {"revision", revision}},
                       {"path", "saved", "projectDirty", "sessionId", "revision"}),
         ActionClass::Destructive, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::ProjectSave},
        {"history_restore", "Restore an earlier state",
         "Move the project through its undo history, as the person's own undo and redo lists do: to just before a step (it and every later step undone) or just after it (it and every earlier step done). Read step IDs from the history section of workspace_inspect. Setting edits, preset choices, the print intent, and the plan are not part of that history and stay as they are; the result lists which of them exist. Waits for approval in JusPrin.",
         object_schema({{"sessionId", id}, {"stepId", id}, {"point", {{"type", "string"}, {"enum", json::array({"before", "after"})}}}},
                       {"sessionId", "stepId", "point"}),
         object_schema({{"history", history_section}, {"notReversed", list_schema(text)}, {"sessionId", id}, {"revision", revision}},
                       {"history", "notReversed", "sessionId", "revision"}),
         ActionClass::Destructive, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::HistoryRestore},
        {"printer_setup", "Set up the printer",
         "Establish the hardware for this job in OrcaSlicer's order: printer preset, plate type, process preset, then filament presets from the first slot. Names come from presets_list; plate types from printer_setup_preview's issues or the printer section. Preview first. If the switch would drop unsaved preset edits, the call is refused unless unsavedEdits is \"discard\", which the user must have agreed to. confirmFacts records what the user said about the physical printer that no sensor reports (for example fact \"plate\", value \"Textured PEI Plate\"; or \"bed_clear\"), each lasting hours (default 24). The result lists what OrcaSlicer replaced on its own. Waits for approval in JusPrin.",
         object_schema({{"printerPreset", string_schema()}, {"plateType", string_schema()}, {"processPreset", string_schema()},
                        {"filamentPresets", {{"type", "array"}, {"items", string_schema()}, {"minItems", 1}, {"maxItems", 16}}},
                        {"unsavedEdits", {{"type", "string"}, {"enum", json::array({"discard"})}}},
                        {"confirmFacts", {{"type", "array"}, {"minItems", 1}, {"maxItems", 16},
                                          {"items", object_schema({{"fact", string_schema()}, {"value", string_schema()},
                                                                   {"hours", {{"type", "integer"}, {"minimum", 1}}}},
                                                                  {"fact", "value"})}}}}),
         object_schema({{"printer", printer_section}, {"processPreset", string_schema()},
                        {"substituted", setup_changes}, {"discarded", setup_edits},
                        {"sessionId", id}, {"revision", revision}},
                       {"printer", "processPreset", "substituted", "discarded", "sessionId", "revision"}),
         ActionClass::Mutation, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::PrinterSetup},
        {"printer_setup_preview", "Preview a printer setup",
         "Dry run of printer_setup with the same preset and plate inputs: whether it is valid and why not, the resulting printer, plate, process and filaments, what OrcaSlicer would replace on its own because it no longer fits, unsaved preset edits the switch would drop, and the mismatches that would remain against the connected printer and the user's confirmed facts. Changes nothing.",
         object_schema({{"printerPreset", string_schema()}, {"plateType", string_schema()}, {"processPreset", string_schema()},
                        {"filamentPresets", {{"type", "array"}, {"items", string_schema()}, {"minItems", 1}, {"maxItems", 16}}}}),
         object_schema({{"valid", boolean_schema()},
                        {"issues", {{"type", "array"}, {"maxItems", 32},
                                    {"items", object_schema({{"code", string_schema()}, {"message", string_schema()}}, {"code", "message"})}}},
                        {"resulting", object_schema({{"printerPreset", string_schema()}, {"plateType", string_schema()},
                                                     {"processPreset", string_schema()},
                                                     {"filamentPresets", {{"type", "array"}, {"items", string_schema()}, {"maxItems", 16}}}},
                                                    {"printerPreset", "plateType", "processPreset", "filamentPresets"})},
                        {"substituted", setup_changes}, {"unsavedEdits", setup_edits},
                        {"mismatches", printer_section["properties"]["mismatches"]},
                        {"sessionId", id}, {"revision", revision}},
                       {"valid", "issues", "resulting", "substituted", "unsavedEdits", "mismatches", "sessionId", "revision"}),
         ActionClass::ReadOnly, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::PrinterSetupPreview},
        {"presets_list", "List printer, filament, or process presets",
         "List the presets of one kind this installation offers, newest compatibility verdict included. Compatible ones only unless you ask for all; a page is not the whole list, so follow nextCursor. Names are what a selection takes; labels are what the user sees.",
         object_schema({{"kind", {{"type", "string"}, {"enum", json::array({"printer", "filament", "process"})}}},
                        {"query", id}, {"compatibleOnly", boolean_schema()},
                        {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 25}}}, {"cursor", id}},
                       {"kind"}),
         object_schema({{"items", array_schema(object_schema({{"name", id}, {"label", id}, {"vendor", string_schema()},
                                                              {"system", boolean_schema()}, {"selected", boolean_schema()},
                                                              {"compatible", boolean_schema()}},
                                                             {"name", "label", "vendor", "system", "selected", "compatible"}), 25)},
                        {"total", revision}, {"nextCursor", id}, {"truncated", boolean_schema()},
                        {"sessionId", id}, {"revision", revision}},
                       {"items", "total", "nextCursor", "truncated", "sessionId", "revision"}),
         ActionClass::ReadOnly, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::PresetsList},
        {"slice_report", "Check the sliced plate",
         "Read what a sliced plate says about itself, by section: summary is time and filament per extruder with weight and cost; findings are Orca's own warnings and errors, the conflicts it detected, and whether a toolpath leaves the bed; material is filament and tool changes and the volume purged for them. A plate that has not been sliced says so rather than failing.",
         object_schema({{"plateId", id}, {"sections", {{"type", "array"},
                                                       {"items", {{"type", "string"},
                                                                  {"enum", json::array({"summary", "findings", "material"})}}},
                                                       {"maxItems", 3}}}}),
         object_schema({{"valid", boolean_schema()}, {"plateId", id},
                        {"summary", object_schema({{"printTimeSeconds", integer_schema()},
                                                   {"prepareTimeSeconds", integer_schema()},
                                                   {"totalGrams", number_schema()}, {"totalCost", number_schema()},
                                                   {"hasCost", boolean_schema()},
                                                   {"filaments", array_schema(object_schema({{"extruder", integer_schema()},
                                                                                             {"lengthMm", number_schema()},
                                                                                             {"grams", number_schema()},
                                                                                             {"cost", number_schema()},
                                                                                             {"hasCost", boolean_schema()}},
                                                                                            {"extruder", "lengthMm", "grams", "cost", "hasCost"}), 16)}},
                                                  {"printTimeSeconds", "prepareTimeSeconds", "totalGrams", "totalCost", "hasCost", "filaments"})},
                        {"findings", object_schema({{"items", array_schema(object_schema({{"code", id}, {"message", text},
                                                                                          {"critical", boolean_schema()},
                                                                                          {"object", string_schema()}},
                                                                                         {"code", "message", "critical", "object"}), 32)},
                                                    {"conflict", text}, {"toolpathOutsideBed", boolean_schema()},
                                                    {"truncated", boolean_schema()}},
                                                   {"items", "conflict", "toolpathOutsideBed", "truncated"})},
                        {"material", object_schema({{"filamentChanges", integer_schema()}, {"extruderChanges", integer_schema()},
                                                    {"purgedMm3", number_schema()}, {"primeTowerMm3", number_schema()},
                                                    {"supportMm3", number_schema()}},
                                                   {"filamentChanges", "extruderChanges", "purgedMm3", "primeTowerMm3", "supportMm3"})},
                        {"sessionId", id}, {"revision", revision}},
                       {"valid", "plateId", "sessionId", "revision"}),
         ActionClass::ReadOnly, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::SliceReportRead},
        {"slice_start", "Slice the plate",
         "Start Orca's own slicing run for one plate, or every plate when you name none, and return once it has started. Read the slicing section of workspace_inspect for the result; it is not ready when this returns. Fails when a slice is already running unless you pass preempt, because nothing records who started that run and it may be the user's. Runs without an approval card unless it preempts.",
         object_schema({{"plateId", id}, {"preempt", boolean_schema()}}),
         object_schema({{"handle", id}, {"started", boolean_schema()}, {"slicing", slicing_section},
                        {"sessionId", id}, {"revision", revision}},
                       {"handle", "started", "slicing", "sessionId", "revision"}),
         ActionClass::Mutation, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::SliceStart,
         true},
        {"workspace_inspect",
         "Inspect the live workspace",
         "Read the open project. The default summary covers plates and objects, setup names, selection IDs, and whether undo and redo are possible. Other sections: project (the file's path and saved state, its own description, designer, license and copyright, packed attachments, and backup state), intent (what this print is for), plan (the plan in force), slicing (whether each plate's slice is current, and a slice in flight), history (the undo steps, for history_restore), printer (the selected printer as configured and as the machine reports it, facts the user confirmed, and where they disagree). IDs are strings scoped to the returned sessionId. No process-setting values are exposed by this tool.",
         object_schema({{"sections", {{"type", "array"},
                                      {"items", {{"type", "string"},
                                                 {"enum", json::array({"summary", "project", "intent", "plan", "slicing", "history", "printer"})}}},
                                      {"maxItems", 7}}}}),
         object_schema({{"intent", intent_section}, {"plan", plan_section}, {"slicing", slicing_section},
                        {"printer", printer_section}, {"project", project_section}, {"sessionId", id}, {"revision", revision}, {"projectName", string_schema()},
                         {"projectDirty", boolean_schema()}, {"printerPreset", string_schema()},
                         {"filamentPreset", string_schema()}, {"activePlateId", id},
                         {"plateCount", revision}, {"objectCount", revision}, {"plates", list_schema(plate_summary)},
                         {"selection", selection_summary}, {"truncated", boolean_schema()},
                         {"history", history_section}},
                         // Only the identity is unconditional. The summary's own fields are
                         // present whenever the summary section is asked for, which is the
                         // default, so a caller that sends no sections sees what it always saw.
                         {"sessionId", "revision", "truncated"}),
         ActionClass::ReadOnly, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::WorkspaceInspect},
        {"record_build",
         "Record a build of the sliced active plate",
         "Record the sliced active plate in the JusPrin manufacturing history.",
         object_schema(json{{"slicerVersion", string_schema()},
                            {"configurationProvenance", string_schema()},
                            {"printTimeSeconds", number_schema()},
                            {"filamentMm", number_schema()},
                            {"materialGrams", number_schema()},
                            {"materialCost", number_schema()},
                            {"layerCount", integer_schema()},
                            {"warnings", string_array_schema()}}),
         object_schema(json{{"buildId", id}, {"recorded", boolean_schema()}},
                       json::array({"buildId", "recorded"})),
         ActionClass::Mutation,
         ToolExposure::Internal,
         ToolAvailability::Always,
         ToolHandler::RecordBuild},
        {"record_export_copy",
         "Record an exported G-code copy",
         "Record a verified external G-code copy linked to a build.",
         object_schema(json{{"buildId", string_schema()},
                            {"destination", string_schema()},
                            {"observedOutputHash", string_schema()}}),
         object_schema(json{{"exportedCopyId", id}, {"buildId", id}},
                       json::array({"exportedCopyId", "buildId"})),
         ActionClass::Destructive,
         ToolExposure::Internal,
         ToolAvailability::Always,
         ToolHandler::RecordExportCopy},
        {"record_physical_print",
         "Record a completed physical print",
         "Record a completed physical-print fact linked to a build.",
         object_schema(json{{"buildId", string_schema()},
                            {"printer", string_schema()},
                            {"material", string_schema()},
                            {"startedAt", string_schema()},
                            {"endedAt", string_schema()},
                            {"outcome", string_schema()},
                            {"failure", string_schema()},
                            {"gcodeHash", string_schema()}}),
         object_schema(json{{"physicalPrintId", id}, {"buildId", id}, {"recorded", boolean_schema()}},
                       json::array({"physicalPrintId", "buildId", "recorded"})),
         ActionClass::Destructive,
         ToolExposure::Internal,
         ToolAvailability::Always,
         ToolHandler::RecordPhysicalPrint},
    };
    std::sort(definitions.begin(), definitions.end(),
              [](const ToolDefinition& lhs, const ToolDefinition& rhs) { return lhs.name < rhs.name; });
    return definitions;
}

} // namespace

namespace {
// Validate the small, closed schema vocabulary the immutable registry uses.
// This is not a validator for client-supplied schemas (none are accepted).
// Unsupported schema keywords are programmer errors, never silently ignored.
bool matches_schema(const json& value, const json& schema)
{
    static const std::set<std::string> supported{"type",    "properties", "required", "additionalProperties",
                                                 "items",   "minimum",    "maxItems", "enum",
                                                 "maxLength"};
    for (const auto& item : schema.items())
        if (!supported.count(item.key())) throw std::logic_error("Unsupported canonical tool schema keyword: " + item.key());
    // A closed vocabulary constrains the value itself whatever its type, so it
    // is checked before the type dispatch: a provenance word, an action state,
    // a section name.
    if (const auto allowed = schema.find("enum");
        allowed != schema.end() &&
        std::none_of(allowed->begin(), allowed->end(), [&value](const json& candidate) { return candidate == value; }))
        return false;
    const std::string type = schema.at("type");
    if (type == "object") {
        if (!value.is_object()) return false;
        for (const auto& required : schema.at("required"))
            if (!value.contains(required.get<std::string>())) return false;
        for (const auto& item : value.items()) {
            const auto& properties = schema.at("properties");
            if (!properties.contains(item.key()) || !matches_schema(item.value(), properties.at(item.key()))) return false;
        }
    } else if (type == "array") {
        if (!value.is_array() || value.size() > schema.value("maxItems", kToolListLimit)) return false;
        for (const auto& item : value) if (!matches_schema(item, schema.at("items"))) return false;
    } else if (type == "string")
        // Counted in UTF-8 bytes, not code points: every output bound this
        // registry states is a byte bound, and so is the truncation the
        // producers apply to meet it.
        return value.is_string() && (!schema.contains("maxLength") ||
                                     value.get_ref<const std::string&>().size() <= schema["maxLength"].get<std::size_t>());
    else if (type == "boolean") return value.is_boolean();
    else if (type == "integer" || type == "number")
        return (type == "integer" ? value.is_number_integer() : value.is_number()) &&
               (!schema.contains("minimum") || value.get<double>() >= schema["minimum"].get<double>());
    else throw std::logic_error("Unsupported canonical tool schema type: " + type);
    return true;
}
}

const ToolRegistry& ToolRegistry::instance()
{
    static const ToolRegistry registry;
    return registry;
}

ToolRegistry::ToolRegistry() : m_definitions(make_definitions()) {}

bool ToolRegistry::validate_output(const ToolDefinition& definition, const json& result) const
{
    return matches_schema(result, definition.output_schema);
}

const ToolDefinition* ToolRegistry::find(std::string_view name) const
{
    const auto found = std::lower_bound(m_definitions.begin(), m_definitions.end(), name,
                                        [](const ToolDefinition& definition, std::string_view key) {
                                            return definition.name < key;
                                        });
    return found != m_definitions.end() && found->name == name ? &*found : nullptr;
}

std::vector<std::reference_wrapper<const ToolDefinition>> ToolRegistry::exposed(ToolExposure exposure) const
{
    std::vector<std::reference_wrapper<const ToolDefinition>> result;
    for (const ToolDefinition& definition : m_definitions)
        if (has_exposure(definition.exposure, exposure))
            result.emplace_back(definition);
    return result;
}

ToolValidationResult ToolRegistry::validate_call(const ToolDefinition& definition,
                                                 const std::string&    arguments_json) const
{
    json arguments = json::parse(arguments_json, nullptr, false);
    if (arguments.is_discarded() || !valid_arguments(definition, arguments))
        return {{}, ToolError{"invalid_arguments", "The tool arguments do not match the registered contract."}};
    if (definition.handler == ToolHandler::SettingsPreviewPatch || definition.handler == ToolHandler::SettingsApplyPatch)
        for (auto& value : arguments["changes"])
            if (!value.is_string()) value = value.is_boolean() ? (value.get<bool>() ? "1" : "0") : value.dump();
    return {arguments.dump(), std::nullopt};
}

bool ToolRegistry::requires_approval(const ToolDefinition& definition, const std::string& arguments_json) const
{
    bool computation_only = definition.computation_only;
    if (definition.handler == ToolHandler::SliceStart) {
        const auto arguments = json::parse(arguments_json, nullptr, false);
        // Slicing itself only replaces a computed result. Taking over a run
        // somebody else may have started is not that, and brings the card.
        if (!arguments.is_discarded() && arguments.value("preempt", false))
            computation_only = false;
    }
    return approval_required(definition.action_class, computation_only);
}

std::string ToolRegistry::approval_title(const ToolDefinition& definition, const std::string& arguments_json) const
{
    if (definition.handler == ToolHandler::IntentUpdate) {
        // The card is where the user confirms the agent understood them, so it
        // shows the interpreted answer rather than the field names.
        const auto arguments = json::parse(arguments_json);
        std::string title = "Record what this print is for: ";
        bool first = true;
        for (const auto& field : arguments.at("fields")) {
            if (!first) title += "; ";
            title += field.at("field").get<std::string>() + " = " +
                     (field.value("value", "").empty() ? "(asked) " + field.value("question", "") : field.value("value", ""));
            first = false;
        }
        if (title.size() > kToolLabelLimit) title.resize(kToolLabelLimit - 3), title += "...";
        return title;
    }
    if (definition.handler == ToolHandler::PrinterSetup) {
        // The coordinator replaces this with the previewed outcome.
        return definition.title;
    }
    if (definition.handler == ToolHandler::SettingsApplyPatch) {
        const auto arguments = json::parse(arguments_json);
        std::string title = "Change " + std::to_string(arguments.at("changes").size()) + " process settings: ";
        bool first = true;
        for (const auto& item : arguments.at("changes").items()) {
            if (!first) title += ", ";
            title += item.key();
            first = false;
        }
        if (title.size() > kToolLabelLimit) title.resize(kToolLabelLimit - 3), title += "...";
        return title;
    }
    return definition.title;
}

} // namespace Slic3r::GUI::JusPrin::Agent
