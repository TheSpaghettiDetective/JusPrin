#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/ToolRegistry.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace Slic3r::GUI::JusPrin::Agent;
using nlohmann::json;

namespace {

bool structurally_valid_schema(const json& schema)
{
    if (!schema.is_object() || schema.value("type", "") != "object" || !schema.contains("properties") ||
        !schema["properties"].is_object() || !schema.contains("required") || !schema["required"].is_array() ||
        schema.value("additionalProperties", true))
        return false;
    for (const json& required : schema["required"])
        if (!required.is_string() || !schema["properties"].contains(required.get<std::string>()))
            return false;
    for (const auto& property : schema["properties"].items())
        if (!property.value().is_object() || !property.value().contains("type") ||
            !property.value()["type"].is_string())
            return false;
    return true;
}

std::vector<std::string> names(const std::vector<std::reference_wrapper<const ToolDefinition>>& definitions)
{
    std::vector<std::string> result;
    for (const ToolDefinition& definition : definitions)
        result.push_back(definition.name);
    return result;
}

} // namespace

TEST_CASE("tool registry definitions are unique deterministic and schema-backed", "[tools][registry]")
{
    const auto& definitions = ToolRegistry::instance().definitions();
    REQUIRE_FALSE(definitions.empty());

    std::vector<std::string> ordered;
    std::set<std::string> unique;
    for (const ToolDefinition& definition : definitions) {
        ordered.push_back(definition.name);
        unique.insert(definition.name);
        CHECK_FALSE(definition.title.empty());
        CHECK_FALSE(definition.description.empty());
        CHECK(structurally_valid_schema(definition.input_schema));
        CHECK(structurally_valid_schema(definition.output_schema));
        CHECK(ToolRegistry::instance().find(definition.name) == &definition);
    }

    CHECK(std::is_sorted(ordered.begin(), ordered.end()));
    CHECK(unique.size() == definitions.size());
    CHECK(ToolRegistry::instance().find("not_a_tool") == nullptr);
}

TEST_CASE("tool registry applies declared adapter exposure", "[tools][registry][exposure]")
{
    CHECK(names(ToolRegistry::instance().exposed(ToolExposure::InApp)) ==
          std::vector<std::string>{"duplicate_object", "import_model", "inspect_selection"});
    CHECK(names(ToolRegistry::instance().exposed(ToolExposure::Mcp)) ==
          std::vector<std::string>{"duplicate_object", "inspect_selection", "workspace_inspect"});
    CHECK(names(ToolRegistry::instance().exposed(ToolExposure::Internal)) ==
          std::vector<std::string>{"record_build", "record_export_copy", "record_physical_print"});

    REQUIRE(ToolRegistry::instance().find("import_model") != nullptr);
    CHECK(ToolRegistry::instance().find("import_model")->availability == ToolAvailability::ImportableAttachment);
}

TEST_CASE("tool registry is the argument validation boundary", "[tools][registry][validation]")
{
    const ToolDefinition& duplicate = *ToolRegistry::instance().find("duplicate_object");
    CHECK(ToolRegistry::instance()
              .validate_call(duplicate, json{{"sessionId", "41"}, {"objectId", "72"}}.dump())
              .valid());
    CHECK_FALSE(ToolRegistry::instance()
                    .validate_call(duplicate, json{{"sessionId", "41"}, {"objectId", 72}}.dump())
                    .valid());
    CHECK_FALSE(ToolRegistry::instance()
                    .validate_call(duplicate,
                                   json{{"sessionId", "41"}, {"objectId", "72"}, {"actionClass", "read_only"}}.dump())
                    .valid());

    const ToolDefinition& inspect = *ToolRegistry::instance().find("inspect_selection");
    CHECK(ToolRegistry::instance().validate_call(inspect, "{}").valid());
    CHECK_FALSE(ToolRegistry::instance().validate_call(inspect, json{{"extra", true}}.dump()).valid());
}
