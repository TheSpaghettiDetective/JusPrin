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
        if (!(apply ? has_only(arguments, {"changes", "expectedSessionId", "expectedRevision"}) : has_only(arguments, {"changes"})) ||
            !arguments.contains("changes") || !arguments["changes"].is_object() || arguments["changes"].empty() ||
            arguments["changes"].size() > 32)
            return false;
        if (apply && (!arguments.contains("expectedSessionId") || !is_unsigned_string(arguments["expectedSessionId"]) ||
                      !arguments.contains("expectedRevision") || !arguments["expectedRevision"].is_number_unsigned()))
            return false;
        return std::all_of(arguments["changes"].begin(), arguments["changes"].end(), [](const auto& value) {
            return value.is_string() || value.is_number() || value.is_boolean();
        });
    }

    if (definition.handler == ToolHandler::InspectSelection || definition.handler == ToolHandler::WorkspaceInspect)
        return arguments.empty();

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
         object_schema({{"changes", changes_input}, {"expectedSessionId", id}, {"expectedRevision", revision}},
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
        {"workspace_inspect",
         "Inspect the live workspace",
         "Read a bounded summary of the open project, plates and objects, setup names, selection IDs, and native history. IDs are strings scoped to the returned sessionId. No process-setting values are exposed by this tool.",
         object_schema(json::object()),
         object_schema({{"sessionId", id}, {"revision", revision}, {"projectName", string_schema()},
                         {"projectDirty", boolean_schema()}, {"printerPreset", string_schema()},
                         {"filamentPreset", string_schema()}, {"activePlateId", id},
                         {"plateCount", revision}, {"objectCount", revision}, {"plates", list_schema(plate_summary)},
                         {"selection", selection_summary}, {"truncated", boolean_schema()},
                         {"history", object_schema({{"canUndo", boolean_schema()}, {"canRedo", boolean_schema()}}, {"canUndo", "canRedo"})}},
                         {"sessionId", "revision", "projectName", "projectDirty", "printerPreset", "filamentPreset", "activePlateId",
                          "plateCount", "objectCount", "plates", "selection", "truncated", "history"}),
         ActionClass::ReadOnly, ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::WorkspaceInspect},
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
    static const std::set<std::string> supported{"type", "properties", "required", "additionalProperties", "items", "minimum", "maxItems"};
    for (const auto& item : schema.items())
        if (!supported.count(item.key())) throw std::logic_error("Unsupported canonical tool schema keyword: " + item.key());
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
    } else if (type == "string") return value.is_string();
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

std::string ToolRegistry::approval_title(const ToolDefinition& definition, const std::string& arguments_json) const
{
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
