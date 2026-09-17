#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/ToolRegistry.hpp"
#include "slic3r/GUI/JusPrin/Agent/ToolResults.hpp"
#include "../jusprin_support/FakeWorkspace.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
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
          std::vector<std::string>{"activity_cancel", "export_file", "history_restore", "intent_update", "object_analyze", "object_divide", "object_divide_preview", "object_import", "object_merge", "object_place", "object_repair", "plan_set", "plate_layout", "presets_list", "printer_list", "printer_setup", "printer_setup_preview", "project_attachment_read", "project_delete_items", "project_open", "project_save", "region_annotate", "settings_apply_patch", "settings_get", "settings_preview_patch", "settings_search", "slice_inspect", "slice_report", "slice_start", "view_render", "workspace_inspect"});
    CHECK(names(ToolRegistry::instance().exposed(ToolExposure::Mcp)) ==
          std::vector<std::string>{"activity_cancel", "export_file", "history_restore", "intent_update", "object_analyze", "object_divide", "object_divide_preview", "object_import_file", "object_merge", "object_place", "object_repair", "plan_set", "plate_layout", "presets_list", "printer_list", "printer_setup", "printer_setup_preview", "project_attachment_read", "project_delete_items", "project_open", "project_save", "region_annotate", "settings_apply_patch", "settings_get", "settings_preview_patch", "settings_search", "slice_inspect", "slice_report", "slice_start", "view_render", "workspace_inspect"});
    CHECK(names(ToolRegistry::instance().exposed(ToolExposure::Internal)) ==
          std::vector<std::string>{"record_build", "record_export_copy", "record_physical_print"});

    REQUIRE(ToolRegistry::instance().find("object_import") != nullptr);
    CHECK(ToolRegistry::instance().find("object_import")->availability == ToolAvailability::ImportableAttachment);
}

TEST_CASE("tool registry is the argument validation boundary", "[tools][registry][validation]")
{
    const ToolDefinition& layout = *ToolRegistry::instance().find("plate_layout");
    const auto copies = [](json object_id) {
        return json::array({json{{"objectId", std::move(object_id)}, {"quantity", 2}}});
    };
    CHECK(ToolRegistry::instance().validate_call(layout, json{{"sessionId", "41"}, {"objects", copies("72")}}.dump()).valid());
    CHECK_FALSE(ToolRegistry::instance().validate_call(layout, json{{"sessionId", "41"}, {"objects", copies(72)}}.dump()).valid());
    CHECK_FALSE(ToolRegistry::instance()
                    .validate_call(layout, json{{"sessionId", "41"}, {"objects", copies("72")}, {"actionClass", "read_only"}}.dump())
                    .valid());

    const ToolDefinition& inspect = *ToolRegistry::instance().find("workspace_inspect");
    CHECK(ToolRegistry::instance().validate_call(inspect, "{}").valid());
    CHECK_FALSE(ToolRegistry::instance().validate_call(inspect, json{{"extra", true}}.dump()).valid());
}

TEST_CASE("Settings schemas validate canonical results and argument decoding is shape-only", "[tools][settings]")
{
    using namespace Slic3r::GUI::JusPrin::Workspace;
    const auto& registry = ToolRegistry::instance();
    FakeWorkspace workspace;
    auto snapshot = workspace.snapshot();
    CHECK(registry.validate_output(*registry.find("settings_search"), settings_search_result(workspace.search_settings({""}), snapshot)));
    CHECK(registry.validate_output(*registry.find("settings_get"), settings_read_result(workspace.read_settings({"layer_height", "bad"}), snapshot)));
    for (auto value : {"0.25", "invalid"}) {
        auto preview = workspace.preview_settings({{{"layer_height", value}}});
        CHECK(registry.validate_output(*registry.find("settings_preview_patch"), settings_preview_result(preview, snapshot)));
        CHECK(registry.validate_output(*registry.find("settings_apply_patch"), settings_apply_result(preview, snapshot, false, false)));
    }
    const auto& preview_tool = *registry.find("settings_preview_patch");
    const auto decoded = registry.validate_call(preview_tool, R"({"changes":{"unknown":true,"wall_loops":4,"layer_height":0.25}})");
    REQUIRE(decoded.valid());
    CHECK(json::parse(decoded.arguments_json)["changes"] == json{{"unknown", "1"}, {"wall_loops", "4"}, {"layer_height", "0.25"}});
    CHECK_FALSE(registry.validate_call(preview_tool, R"({"changes":{}})").valid());
    CHECK_FALSE(registry.validate_call(preview_tool, R"({"changes":{"wall_loops":null}})").valid());
    CHECK_FALSE(registry.validate_call(*registry.find("settings_apply_patch"), R"({"changes":{"wall_loops":4}})").valid());
    auto unsupported = preview_tool;
    unsupported.output_schema["maximum"] = 1;
    CHECK_THROWS_AS(registry.validate_output(unsupported, settings_preview_result({}, snapshot)), std::logic_error);
    CHECK(registry.approval_title(*registry.find("settings_apply_patch"), decoded.arguments_json).find("wall_loops") != std::string::npos);
}

TEST_CASE("output validation understands closed vocabularies and string bounds", "[tools][registry][validation]")
{
    const auto& registry = ToolRegistry::instance();
    // The vocabulary is exercised on a definition built here rather than on a
    // shipped tool: the keywords land before the first catalog row needs them.
    ToolDefinition definition = *registry.find("workspace_inspect");
    definition.output_schema = json{{"type", "object"},
                                    {"properties",
                                     {{"provenance", {{"type", "string"}, {"enum", json::array({"file", "observed"})}}},
                                      {"label", {{"type", "string"}, {"maxLength", 8}}}}},
                                    {"required", json::array({"provenance", "label"})},
                                    {"additionalProperties", false}};

    CHECK(registry.validate_output(definition, json{{"provenance", "observed"}, {"label", "front"}}));
    CHECK_FALSE(registry.validate_output(definition, json{{"provenance", "guessed"}, {"label", "front"}}));
    CHECK_FALSE(registry.validate_output(definition, json{{"provenance", "file"}, {"label", "far too long"}}));
    // Eight bytes, four code points: the bound the producers truncate to is
    // the bound this validator enforces.
    CHECK_FALSE(registry.validate_output(definition, json{{"provenance", "file"}, {"label", "ééééé"}}));
    CHECK(registry.validate_output(definition, json{{"provenance", "file"}, {"label", "éééé"}}));

    // enum is not string-only, and an unsupported keyword is still a defect.
    definition.output_schema["properties"]["provenance"] = json{{"type", "integer"}, {"enum", json::array({1, 2})}};
    CHECK(registry.validate_output(definition, json{{"provenance", 2}, {"label", "front"}}));
    CHECK_FALSE(registry.validate_output(definition, json{{"provenance", 3}, {"label", "front"}}));
    definition.output_schema["properties"]["label"]["pattern"] = "^f";
    CHECK_THROWS_AS(registry.validate_output(definition, json{{"provenance", 1}, {"label", "front"}}), std::logic_error);
}
