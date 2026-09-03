#include "ToolRegistry.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <set>
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

    if (definition.handler == ToolHandler::InspectSelection)
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

    std::vector<ToolDefinition> definitions{
        {"duplicate_object",
         "Duplicate project object",
         "Propose duplicating one existing object in the current project.",
         object_schema(json{{"sessionId", string_schema()}, {"objectId", string_schema()}},
                       json::array({"sessionId", "objectId"})),
         object_schema(json{{"revision", revision}, {"newObjectId", id}}, json::array({"revision"})),
         ActionClass::Mutation,
         ToolExposure::InApp | ToolExposure::Mcp,
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
         object_schema(json{{"selection", string_array_schema()}, {"revision", revision}},
                       json::array({"selection", "revision"})),
         ActionClass::ReadOnly,
         ToolExposure::InApp | ToolExposure::Mcp,
         ToolAvailability::Always,
         ToolHandler::InspectSelection},
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

const ToolRegistry& ToolRegistry::instance()
{
    static const ToolRegistry registry;
    return registry;
}

ToolRegistry::ToolRegistry() : m_definitions(make_definitions()) {}

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
    const json arguments = json::parse(arguments_json, nullptr, false);
    if (arguments.is_discarded() || !valid_arguments(definition, arguments))
        return {{}, ToolError{"invalid_arguments", "The tool arguments do not match the registered contract."}};
    return {arguments.dump(), std::nullopt};
}

std::string ToolRegistry::approval_title(const ToolDefinition& definition, const std::string&) const
{
    return definition.title;
}

} // namespace Slic3r::GUI::JusPrin::Agent
