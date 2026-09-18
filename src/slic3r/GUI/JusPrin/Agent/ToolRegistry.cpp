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

json printer_spool_schema()
{
    return object_schema(json{{"name", string_schema()}, {"material", string_schema()}, {"colour", string_schema()}},
                         json::array({"name", "material"}));
}

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

// A settings call's optional target: one object, by id.
bool optional_settings_target(const json& arguments)
{
    if (!arguments.contains("target"))
        return true;
    const json& target = arguments["target"];
    return target.is_object() && has_only(target, {"objectId"}) && target.contains("objectId") && is_unsigned_string(target["objectId"]);
}

bool valid_arguments(const ToolDefinition& definition, const json& arguments)
{
    if (!arguments.is_object())
        return false;

    if (definition.handler == ToolHandler::SettingsSearch)
        return has_only(arguments, {"query", "limit", "cursor", "writable", "changedOnly"}) && arguments.contains("query") &&
               arguments["query"].is_string() && optional_string(arguments, "cursor") &&
               (!arguments.contains("writable") || arguments["writable"].is_boolean()) &&
               (!arguments.contains("changedOnly") || arguments["changedOnly"].is_boolean()) &&
               (!arguments.contains("limit") || (arguments["limit"].is_number_unsigned() &&
                 arguments["limit"].get<std::uint64_t>() >= 1 && arguments["limit"].get<std::uint64_t>() <= 25));
    if (definition.handler == ToolHandler::SettingsGet) {
        if (!has_only(arguments, {"keys", "target"}) || !optional_settings_target(arguments) || !arguments.contains("keys") ||
            !arguments["keys"].is_array() ||
            arguments["keys"].empty() || arguments["keys"].size() > 32)
            return false;
        return std::all_of(arguments["keys"].begin(), arguments["keys"].end(), [](const auto& key) { return key.is_string(); });
    }
    if (definition.handler == ToolHandler::SettingsPreviewPatch || definition.handler == ToolHandler::SettingsApplyPatch) {
        const bool apply = definition.handler == ToolHandler::SettingsApplyPatch;
        if (!(apply ? has_only(arguments, {"changes", "target", "expectedSessionId", "expectedRevision", "intent"}) :
                      has_only(arguments, {"changes", "target"})) ||
            !optional_settings_target(arguments) ||
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


    if (definition.handler == ToolHandler::WorkspaceInspect) {
        if (!has_only(arguments, {"sections"}))
            return false;
        if (!arguments.contains("sections"))
            return true; // the summary, as every caller before sections existed asked for
        const json& sections = arguments["sections"];
        if (!sections.is_array() || sections.empty() || sections.size() > 9)
            return false;
        std::set<std::string> seen;
        for (const auto& section : sections) {
            if (!section.is_string())
                return false;
            const std::string& name = section.get_ref<const std::string&>();
            if ((name != "summary" && name != "intent" && name != "plan" && name != "slicing" && name != "history" && name != "printer" && name != "project" && name != "objects" && name != "activities") ||
                !seen.insert(name).second)
                return false;
        }
        return true;
    }

    if (definition.handler == ToolHandler::PrinterList)
        return arguments.empty();

    if (definition.handler == ToolHandler::ObjectImport || definition.handler == ToolHandler::ObjectImportFile) {
        const char* source = definition.handler == ToolHandler::ObjectImport ? "attachmentId" : "path";
        if (!has_only(arguments, {"sessionId", source, "plateId", "unitConversion", "oversized"}) ||
            !arguments.contains("sessionId") || !is_unsigned_string(arguments["sessionId"]) || !arguments.contains(source) ||
            !arguments[source].is_string() || arguments[source].get_ref<const std::string&>().empty() ||
            arguments[source].get_ref<const std::string&>().size() > 1024)
            return false;
        if (arguments.contains("plateId") && !is_unsigned_string(arguments["plateId"]))
            return false;
        const auto one_of = [&arguments](const char* key, std::initializer_list<const char*> allowed) {
            return !arguments.contains(key) ||
                   std::any_of(allowed.begin(), allowed.end(), [&](const char* value) { return arguments[key] == value; });
        };
        return one_of("unitConversion", {"keep", "convertIfTiny", "inches"}) && one_of("oversized", {"keep", "scaleToFit"});
    }

    if (definition.handler == ToolHandler::ProjectDeleteItems) {
        if (!has_only(arguments, {"sessionId", "items"}) || arguments.size() != 2 || !is_unsigned_string(arguments["sessionId"]))
            return false;
        const json& items = arguments["items"];
        if (!items.is_array() || items.empty() || items.size() > 32)
            return false;
        return std::all_of(items.begin(), items.end(), [](const json& item) {
            if (!item.is_object())
                return false;
            if (item.contains("plateId"))
                return item.size() == 1 && is_unsigned_string(item["plateId"]);
            if (item.contains("regionId"))
                return item.size() == 1 && item["regionId"].is_string() && !item["regionId"].get_ref<const std::string&>().empty() &&
                       item["regionId"].get_ref<const std::string&>().size() <= 16;
            if (!has_only(item, {"objectId", "partId", "instance"}) || !item.contains("objectId") ||
                !is_unsigned_string(item["objectId"]) || (item.contains("partId") && item.contains("instance")))
                return false;
            return (!item.contains("partId") || is_unsigned_string(item["partId"])) &&
                   (!item.contains("instance") || item["instance"].is_number_unsigned());
        });
    }

    if (definition.handler == ToolHandler::PlateLayout) {
        if (!has_only(arguments, {"sessionId", "objects", "plates", "arrange"}) || !arguments.contains("sessionId") ||
            !is_unsigned_string(arguments["sessionId"]) || arguments.size() < 2)
            return false;
        const auto text = [](const json& value) {
            return value.is_string() && !value.get_ref<const std::string&>().empty() && value.get_ref<const std::string&>().size() <= kToolLabelLimit;
        };
        if (arguments.contains("objects")) {
            const json& rows = arguments["objects"];
            if (!rows.is_array() || rows.empty() || rows.size() > 64)
                return false;
            std::set<std::string> seen;
            for (const json& row : rows) {
                if (!has_only(row, {"objectId", "enabled", "quantity", "plateId", "name", "extruder"}) || !row.contains("objectId") ||
                    !is_unsigned_string(row["objectId"]) || row.size() < 2 || !seen.insert(row["objectId"].get<std::string>()).second)
                    return false;
                if (row.contains("enabled") && !row["enabled"].is_boolean()) return false;
                if (row.contains("quantity") && (!row["quantity"].is_number_unsigned() || row["quantity"].get<std::uint64_t>() < 1 ||
                                                 row["quantity"].get<std::uint64_t>() > 64))
                    return false;
                if (row.contains("plateId") && !is_unsigned_string(row["plateId"])) return false;
                if (row.contains("name") && !text(row["name"])) return false;
                if (row.contains("extruder") && (!row["extruder"].is_number_unsigned() || row["extruder"].get<std::uint64_t>() < 1 ||
                                                 row["extruder"].get<std::uint64_t>() > 16))
                    return false;
            }
        }
        if (arguments.contains("plates")) {
            const json& rows = arguments["plates"];
            if (!rows.is_array() || rows.empty() || rows.size() > 16)
                return false;
            for (const json& row : rows) {
                if (!has_only(row, {"plateId", "name", "bedType"}) || (row.contains("plateId") && row.size() < 2)) return false;
                if (row.contains("plateId") && !is_unsigned_string(row["plateId"])) return false;
                if (row.contains("name") && !text(row["name"])) return false;
                if (row.contains("bedType") && !text(row["bedType"])) return false;
            }
        }
        if (arguments.contains("arrange")) {
            const json& arrange = arguments["arrange"];
            if (!has_only(arrange, {"plateId", "spacingMm", "allowRotation"})) return false;
            if (arrange.contains("plateId") && !is_unsigned_string(arrange["plateId"])) return false;
            if (arrange.contains("spacingMm") && (!arrange["spacingMm"].is_number() || arrange["spacingMm"].get<double>() < 0 ||
                                                  arrange["spacingMm"].get<double>() > 100))
                return false;
            if (arrange.contains("allowRotation") && !arrange["allowRotation"].is_boolean()) return false;
        }
        return true;
    }

    if (definition.handler == ToolHandler::ObjectPlace) {
        if (!has_only(arguments, {"sessionId", "objectId", "instance", "unitsFix", "scale", "scaleTo", "mirrorAxis",
                                  "faceDown", "rotateDegrees", "position", "dropToBed", "autoOrient"}) ||
            !arguments.contains("sessionId") || !is_unsigned_string(arguments["sessionId"]) ||
            !arguments.contains("objectId") || !is_unsigned_string(arguments["objectId"]))
            return false;
        const auto numbers = [](const json& value, std::size_t count) {
            return value.is_array() && value.size() == count &&
                   std::all_of(value.begin(), value.end(), [](const json& v) { return v.is_number(); });
        };
        if (arguments.contains("instance") && !arguments["instance"].is_number_unsigned())
            return false;
        if (arguments.contains("unitsFix") && arguments["unitsFix"] != "inches" && arguments["unitsFix"] != "meters")
            return false;
        if (arguments.contains("scale")) {
            const json& scale = arguments["scale"];
            if (!numbers(scale, 3) || !std::all_of(scale.begin(), scale.end(), [](const json& v) { return v.get<double>() > 0; }))
                return false;
        }
        if (arguments.contains("scaleTo")) {
            const json& target = arguments["scaleTo"];
            if (!has_only(target, {"axis", "sizeMm"}) || target.size() != 2 || !target["axis"].is_string() ||
                (target["axis"] != "x" && target["axis"] != "y" && target["axis"] != "z") || !target["sizeMm"].is_number() ||
                target["sizeMm"].get<double>() <= 0)
                return false;
        }
        if (arguments.contains("mirrorAxis") && arguments["mirrorAxis"] != "x" && arguments["mirrorAxis"] != "y" &&
            arguments["mirrorAxis"] != "z")
            return false;
        if (arguments.contains("faceDown") && (!arguments["faceDown"].is_string() ||
                                               arguments["faceDown"].get_ref<const std::string&>().empty() ||
                                               arguments["faceDown"].get_ref<const std::string&>().size() > 64))
            return false;
        if (arguments.contains("rotateDegrees") && !numbers(arguments["rotateDegrees"], 3))
            return false;
        if (arguments.contains("position") && !numbers(arguments["position"], 2))
            return false;
        for (const char* flag : {"dropToBed", "autoOrient"})
            if (arguments.contains(flag) && arguments[flag] != true)
                return false;
        // Facets that decide the same thing twice are refused rather than ordered.
        const int orientation = int(arguments.contains("faceDown")) + int(arguments.contains("rotateDegrees")) +
                                int(arguments.contains("autoOrient"));
        const int sizing = int(arguments.contains("scale")) + int(arguments.contains("scaleTo")) + int(arguments.contains("unitsFix"));
        const bool anything = arguments.size() > 2 + std::size_t(arguments.contains("instance"));
        return orientation <= 1 && sizing <= 1 && anything;
    }

    if (definition.handler == ToolHandler::ObjectDividePreview || definition.handler == ToolHandler::ObjectDivide) {
        if (!has_only(arguments, {"sessionId", "objectId", "plane", "shells"}) || arguments.size() != 3 ||
            !is_unsigned_string(arguments["sessionId"]) || !arguments.contains("objectId") || !is_unsigned_string(arguments["objectId"]))
            return false;
        if (arguments.contains("shells"))
            return arguments["shells"] == "objects" || arguments["shells"] == "parts";
        const json& plane = arguments["plane"];
        const auto  numbers = [](const json& value) {
            return value.is_array() && value.size() == 3 &&
                   std::all_of(value.begin(), value.end(), [](const json& v) { return v.is_number(); });
        };
        return plane.is_object() && has_only(plane, {"point", "normal", "keep", "asParts"}) && plane.contains("point") &&
               plane.contains("normal") && numbers(plane["point"]) && numbers(plane["normal"]) &&
               (!plane.contains("keep") || plane["keep"] == "both" || plane["keep"] == "upper" || plane["keep"] == "lower") &&
               (!plane.contains("asParts") || plane["asParts"].is_boolean());
    }

    if (definition.handler == ToolHandler::ViewRender) {
        const auto size = [&arguments](const char* key) {
            return !arguments.contains(key) || (arguments[key].is_number_unsigned() && arguments[key].get<std::uint64_t>() >= 64 &&
                                                arguments[key].get<std::uint64_t>() <= 1280);
        };
        static const std::set<std::string> views{"iso", "front", "rear", "left", "right", "top", "bottom", "top_front", "plate"};
        return has_only(arguments, {"plateId", "view", "widthPx", "heightPx"}) &&
               (!arguments.contains("plateId") || is_unsigned_string(arguments["plateId"])) &&
               (!arguments.contains("view") || (arguments["view"].is_string() && views.count(arguments["view"].get<std::string>()))) &&
               size("widthPx") && size("heightPx");
    }

    if (definition.handler == ToolHandler::AttachmentRead)
        return has_only(arguments, {"id"}) && arguments.size() == 1 && arguments["id"].is_string() &&
               !arguments["id"].get_ref<const std::string&>().empty() && arguments["id"].get_ref<const std::string&>().size() <= 512;

    if (definition.handler == ToolHandler::SliceInspect) {
        if (!has_only(arguments, {"plateId", "view", "first", "count"}) || !arguments.contains("view") ||
            (arguments["view"] != "layers" && arguments["view"] != "gcode"))
            return false;
        return (!arguments.contains("plateId") || is_unsigned_string(arguments["plateId"])) &&
               (!arguments.contains("first") || arguments["first"].is_number_unsigned()) &&
               (!arguments.contains("count") || (arguments["count"].is_number_unsigned() && arguments["count"].get<std::uint64_t>() >= 1 &&
                                                 arguments["count"].get<std::uint64_t>() <= 100));
    }

    if (definition.handler == ToolHandler::ActivityCancel)
        return has_only(arguments, {"handle"}) && arguments.size() == 1 && arguments["handle"].is_string() &&
               !arguments["handle"].get_ref<const std::string&>().empty() && arguments["handle"].get_ref<const std::string&>().size() <= 64;

    if (definition.handler == ToolHandler::ExportFile) {
        static const std::set<std::string> kinds{"gcode", "sliced_3mf", "project_3mf", "stl", "presets"};
        if (!has_only(arguments, {"sessionId", "kind", "path", "plateId", "objectIds", "overwrite"}) ||
            !arguments.contains("sessionId") || !is_unsigned_string(arguments["sessionId"]) || !arguments.contains("kind") ||
            !arguments["kind"].is_string() || !kinds.count(arguments["kind"].get<std::string>()) || !arguments.contains("path") ||
            !arguments["path"].is_string() || arguments["path"].get_ref<const std::string&>().empty() ||
            arguments["path"].get_ref<const std::string&>().size() > 1024)
            return false;
        if (arguments.contains("plateId") && (!is_unsigned_string(arguments["plateId"]) || arguments["kind"] == "project_3mf" ||
                                              arguments["kind"] == "presets"))
            return false;
        if (arguments.contains("objectIds")) {
            const json& ids = arguments["objectIds"];
            if (arguments["kind"] != "stl" || arguments.contains("plateId") || !ids.is_array() || ids.empty() || ids.size() > 64 ||
                !std::all_of(ids.begin(), ids.end(), [](const json& id) { return is_unsigned_string(id); }))
                return false;
        }
        return !arguments.contains("overwrite") || arguments["overwrite"].is_boolean();
    }

    if (definition.handler == ToolHandler::ObjectMerge) {
        if (!has_only(arguments, {"sessionId", "objectIds"}) || arguments.size() != 2 || !is_unsigned_string(arguments["sessionId"]))
            return false;
        const json& ids = arguments["objectIds"];
        std::set<std::string> seen;
        return ids.is_array() && ids.size() >= 2 && ids.size() <= 16 && std::all_of(ids.begin(), ids.end(), [&seen](const json& id) {
                   return is_unsigned_string(id) && seen.insert(id.get<std::string>()).second;
               });
    }

    if (definition.handler == ToolHandler::ObjectRepair)
        return has_only(arguments, {"sessionId", "objectId"}) && arguments.size() == 2 && is_unsigned_string(arguments["sessionId"]) &&
               arguments.contains("objectId") && is_unsigned_string(arguments["objectId"]);

    if (definition.handler == ToolHandler::RegionAnnotate) {
        if (!has_only(arguments, {"sessionId", "regions"}) || arguments.size() != 2 || !is_unsigned_string(arguments["sessionId"]))
            return false;
        const json& rows = arguments["regions"];
        if (!rows.is_array() || rows.empty() || rows.size() > 32)
            return false;
        const auto numbers = [](const json& value) {
            return value.is_array() && value.size() == 3 &&
                   std::all_of(value.begin(), value.end(), [](const json& v) { return v.is_number(); });
        };
        const auto positive = [](const json& object, const char* key) {
            return object.contains(key) && object[key].is_number() && object[key].get<double>() > 0;
        };
        return std::all_of(rows.begin(), rows.end(), [&](const json& row) {
            if (!row.is_object() || !has_only(row, {"regionId", "objectId", "kind", "geometry", "settings", "extruder"}))
                return false;
            const bool existing = row.contains("regionId");
            if (existing && (!row["regionId"].is_string() || row["regionId"].get_ref<const std::string&>().empty() ||
                             row["regionId"].get_ref<const std::string&>().size() > 16))
                return false;
            // A new region names its object, kind and geometry; an existing one
            // may name any of them to change it.
            if (!existing && (!row.contains("objectId") || !row.contains("kind") || !row.contains("geometry")))
                return false;
            if (row.contains("objectId") && !is_unsigned_string(row["objectId"]))
                return false;
            if (row.contains("kind") && (!row["kind"].is_string() || row["kind"].get_ref<const std::string&>().size() > 32))
                return false;
            if (row.contains("extruder") && (!row["extruder"].is_number_unsigned() || row["extruder"].get<std::uint64_t>() < 1 ||
                                             row["extruder"].get<std::uint64_t>() > 16))
                return false;
            if (row.contains("settings")) {
                const json& settings = row["settings"];
                if (!settings.is_object() || settings.empty() || settings.size() > 8 ||
                    !std::all_of(settings.begin(), settings.end(), [](const json& v) { return v.is_string() || v.is_number() || v.is_boolean(); }))
                    return false;
            }
            if (!row.contains("geometry"))
                return true;
            const json& geometry = row["geometry"];
            if (!geometry.is_object() || !geometry.contains("type") || !geometry["type"].is_string())
                return false;
            const std::string type = geometry["type"];
            if (type == "face" || type == "hole")
                return has_only(geometry, {"type", "handle"}) && geometry.size() == 2 && geometry["handle"].is_string() &&
                       !geometry["handle"].get_ref<const std::string&>().empty() && geometry["handle"].get_ref<const std::string&>().size() <= 64;
            if (type == "box")
                return has_only(geometry, {"type", "center", "sizeMm"}) && geometry.size() == 3 && numbers(geometry["center"]) &&
                       numbers(geometry["sizeMm"]);
            if (type == "cylinder")
                return has_only(geometry, {"type", "center", "axis", "diameterMm", "lengthMm"}) && geometry.size() == 5 &&
                       numbers(geometry["center"]) && numbers(geometry["axis"]) && positive(geometry, "diameterMm") && positive(geometry, "lengthMm");
            if (type == "direction")
                return has_only(geometry, {"type", "vector", "toleranceDegrees"}) && geometry.size() == 3 && numbers(geometry["vector"]) &&
                       positive(geometry, "toleranceDegrees") && geometry["toleranceDegrees"].get<double>() <= 90;
            return type == "object" && geometry.size() == 1;
        });
    }

    if (definition.handler == ToolHandler::ObjectAnalyze) {
        if (!has_only(arguments, {"sessionId", "objectId", "include", "measure", "candidates"}) || !arguments.contains("sessionId") ||
            !is_unsigned_string(arguments["sessionId"]) || !arguments.contains("objectId") ||
            !is_unsigned_string(arguments["objectId"]) || !arguments.contains("include"))
            return false;
        const json& include = arguments["include"];
        if (!include.is_array() || include.empty() || include.size() > 6)
            return false;
        std::set<std::string> seen;
        for (const auto& section : include)
            if (!section.is_string() || !seen.insert(section.get<std::string>()).second ||
                (section != "mesh" && section != "features" && section != "fit" && section != "measure" &&
                 section != "orientations" && section != "regions"))
                return false;
        if (arguments.contains("candidates")) {
            const json& candidates = arguments["candidates"];
            if (!seen.count("orientations") || !candidates.is_array() || candidates.empty() || candidates.size() > 8)
                return false;
            for (const auto& candidate : candidates) {
                if (!candidate.is_object() || candidate.size() != 1)
                    return false;
                if (candidate.contains("up")) {
                    const json& up = candidate["up"];
                    if (!up.is_array() || up.size() != 3 || !std::all_of(up.begin(), up.end(), [](const json& v) { return v.is_number(); }))
                        return false;
                } else if (!candidate.contains("faceDown") || !candidate["faceDown"].is_string() ||
                           candidate["faceDown"].get_ref<const std::string&>().empty() ||
                           candidate["faceDown"].get_ref<const std::string&>().size() > 64) {
                    return false;
                }
            }
        }
        const bool measuring = seen.count("measure") > 0;
        if (measuring != arguments.contains("measure"))
            return false;
        if (!measuring)
            return true;
        const json& measure = arguments["measure"];
        return has_only(measure, {"from", "to"}) && measure.size() == 2 && measure["from"].is_string() &&
               measure["to"].is_string() && !measure["from"].get_ref<const std::string&>().empty() &&
               !measure["to"].get_ref<const std::string&>().empty() &&
               measure["from"].get_ref<const std::string&>().size() <= 64 && measure["to"].get_ref<const std::string&>().size() <= 64;
    }

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

    // The printer tools check the shape here. How many printers, and which
    // sizes, are answered by the printer session with errors of their own,
    // because those answers are what the model says next -- so a list of
    // thirty printers passes here and is refused there as too many. A nozzle
    // of null or 0 is how the model leaves it unsaid, and means that.
    const auto valid_nozzle = [&arguments] {
        return !arguments.contains("nozzle") || arguments["nozzle"].is_null() ||
               (arguments["nozzle"].is_number() && arguments["nozzle"].get<double>() >= 0. && arguments["nozzle"].get<double>() <= 5.);
    };

    if (definition.handler == ToolHandler::PrinterIdentify) {
        if (!has_only(arguments, {"catalogIds", "nozzle"}) || !arguments.contains("catalogIds") || !valid_nozzle())
            return false;
        const json& ids = arguments["catalogIds"];
        if (!ids.is_array() || ids.empty() || ids.size() > kToolListLimit)
            return false;
        return std::all_of(ids.begin(), ids.end(), [](const json& id) {
            return id.is_string() && !id.get_ref<const std::string&>().empty() && id.get_ref<const std::string&>().size() <= kToolLabelLimit;
        });
    }

    if (definition.handler == ToolHandler::PrinterChange) {
        if (!has_only(arguments, {"nozzle", "spools"}) || !valid_nozzle())
            return false;
        if (!arguments.contains("spools"))
            return true;
        const json& spools = arguments["spools"];
        if (!spools.is_array() || spools.size() > 16)
            return false;
        return std::all_of(spools.begin(), spools.end(), [](const json& spool) {
            return has_only(spool, {"name", "material", "colour"}) && spool.contains("name") && spool.contains("material") &&
                   optional_text(spool, "name") && optional_text(spool, "material") && optional_text(spool, "colour");
        });
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
        if (!sections.is_array() || sections.empty() || sections.size() > 8)
            return false;
        std::set<std::string> seen;
        for (const auto& section : sections) {
            if (!section.is_string())
                return false;
            const std::string& name = section.get_ref<const std::string&>();
            if ((name != "summary" && name != "findings" && name != "material" && name != "supports" && name != "seams" &&
                 name != "firstLayer" && name != "islands" && name != "intent") ||
                !seen.insert(name).second)
                return false;
        }
        return true;
    }

    if (definition.handler == ToolHandler::SliceStart)
        return has_only(arguments, {"plateId", "preempt", "wait"}) &&
               (!arguments.contains("plateId") || is_unsigned_string(arguments["plateId"])) &&
               (!arguments.contains("preempt") || arguments["preempt"].is_boolean()) &&
               (!arguments.contains("wait") || arguments["wait"].is_boolean());

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
    const json vector3 = {{"type", "array"}, {"items", number_schema()}, {"maxItems", 3}};
    const json region_text{{"type", "string"}, {"maxLength", kToolTextLimit}};
    const json piece_row = object_schema({{"objectId", id}, {"name", region_text}, {"volumeMm3", number_schema()},
                                          {"sizeMm", vector3}, {"centerMm", vector3}, {"overhangAreaMm2", number_schema()}},
                                         {"name", "volumeMm3", "sizeMm", "centerMm", "overhangAreaMm2"});
    const json before_after = object_schema({{"before", number_schema()}, {"after", number_schema()}}, {"before", "after"});
    const json region_row = object_schema({{"regionId", id}, {"objectId", id}, {"kind", id}, {"label", region_text},
                                           {"geometry", id}, {"artifacts", {{"type", "array"}, {"items", region_text}, {"maxItems", 8}}},
                                           {"bindingLost", boolean_schema()}, {"artifactsMissing", boolean_schema()}},
                                          {"regionId", "kind", "label", "geometry", "artifacts"});
    const json objects_section = list_schema(object_schema(
        {{"objectId", id}, {"name", string_schema()}, {"plateIds", {{"type", "array"}, {"items", id}}},
         {"instanceCount", integer_schema()}, {"partCount", integer_schema()}, {"modifierCount", integer_schema()},
         {"negativePartCount", integer_schema()}, {"supportVolumeCount", integer_schema()}, {"printable", boolean_schema()},
         {"extruder", integer_schema()}, {"sizeMm", vector3}, {"overrideCount", integer_schema()}},
        {"objectId", "name", "plateIds", "instanceCount", "partCount", "modifierCount", "printable", "sizeMm", "overrideCount"}));
    const json import_result = object_schema(
        {{"objectIds", {{"type", "array"}, {"items", id}, {"maxItems", 64}}},
         {"objects", objects_section},
         {"decisions", {{"type", "array"}, {"maxItems", 32},
                        {"items", object_schema({{"question", string_schema()},
                                                 {"answer", {{"type", "string"}, {"enum", json::array({"yes", "no", "ok", "cancel"})}}}},
                                                {"question", "answer"})}}},
         {"sessionId", id}, {"revision", revision}},
        {"objectIds", "objects", "decisions", "sessionId", "revision"});
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
    json settings_target = object_schema({{"objectId", id}}, {"objectId"});
    settings_target["description"] = "Omit for the print's own settings, which every object uses. Only when the user asks for "
                                     "one particular object to be printed differently from the others.";
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
    const json jobs_list = {{"type", "array"}, {"maxItems", 16},
                            {"items", object_schema({{"handle", id},
                                                     {"kind", {{"type", "string"}, {"enum", json::array({"orient", "arrange"})}}},
                                                     {"state", {{"type", "string"}, {"enum", json::array({"running", "finished", "cancelled", "failed"})}}},
                                                     {"notPlaced", {{"type", "array"}, {"items", id}, {"maxItems", 64}}}},
                                                    {"handle", "kind", "state", "notPlaced"})}};
    const json slicing_section = object_schema({{"running", boolean_schema()}, {"plateId", id},
                                                {"percent", integer_schema()}, {"handle", id},
                                                {"plates", array_schema(slicing_plate, 16)},
                                                {"jobs", jobs_list},
                                                {"truncated", boolean_schema()}},
                                               {"running", "plates", "jobs", "truncated"});

    std::vector<ToolDefinition> definitions{
        {"settings_search", "Search process settings",
         "Find a page of process settings by key, label, or description. The query may hold several words or keys, and settings matching more of them come first. Requires an active FFF process preset. writable keeps only the settings the patch tools may change; changedOnly keeps only settings that differ from the saved preset, which with an empty query lists every unsaved change. A page is not the full list; read known keys directly with settings_get or follow nextCursor.",
         object_schema({{"query", id}, {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 25}}}, {"cursor", id},
                        {"writable", boolean_schema()}, {"changedOnly", boolean_schema()}}, {"query"}),
         settings_output({{"items", array_schema(setting_def, 25)}, {"nextCursor", id}}, {"items", "nextCursor"}),
         ActionClass::ReadOnly, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::SettingsSearch},
        {"settings_get", "Read process settings",
         "Read current process values and their preset origin. Requires an active FFF process preset; read before proposing a patch. Leave target out to read the process values every object prints with. With target.objectId (an id from workspace_inspect), reads the values that one object prints with: its own override where it has one (overridden: true), otherwise the process value.",
         object_schema({{"keys", {{"type", "array"}, {"items", id}, {"minItems", 1}, {"maxItems", 32}}}, {"target", settings_target}}, {"keys"}),
         settings_output({{"items", array_schema(object_schema({{"key", id}, {"value", id}, {"type", id}, {"label", id},
             {"unit", id}, {"differsFromPreset", boolean_schema()}, {"differsFromSystem", boolean_schema()}, {"writable", boolean_schema()},
             {"overridden", boolean_schema()}},
             {"key", "value", "type", "label", "unit", "differsFromPreset", "differsFromSystem", "writable"}), 32)},
             {"unknownKeys", array_schema(issue, 32)}}, {"items", "unknownKeys"}),
         ActionClass::ReadOnly, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::SettingsGet},
        {"settings_preview_patch", "Preview process settings",
         "Validate an atomic process-settings patch without changing the workspace. Requires an active FFF process preset; use the returned sessionId and revision when applying. Leave target out to change the process, which every object on every plate prints with; that is what a request about the print means. Use target.objectId (an id from workspace_inspect) only when the user asks for one object to differ from the others; a selected object, or a project with a single object, is not such a request; the patch is then checked as overrides on that object: settings that apply to the whole print (skirt, travel speed, spiral vase and the like) are refused with unsupported_scope, and no dependencies are predicted because OrcaSlicer keeps object overrides as written.",
         object_schema({{"changes", changes_input}, {"target", settings_target}}, {"changes"}), patch_output,
         ActionClass::ReadOnly, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::SettingsPreviewPatch},
        {"settings_apply_patch", "Change process settings",
         "Apply an atomic process-settings patch. Requires an active FFF process preset and the sessionId and revision from a preview of the same changes and target; edits to the model since that preview do not matter, a settings change does. Calling it shows the user an approval card in JusPrin and waits for their decision. Leave target out, as in the preview, unless the user asked for one object to differ. A process change is not undone by project Undo; with target.objectId the change becomes that object's own override, and project Undo does undo it.",
         object_schema({{"changes", changes_input}, {"target", settings_target}, {"expectedSessionId", id}, {"expectedRevision", revision},
                        {"intent", json{{"type", "string"}, {"maxLength", 40},
                                        {"description", "What the user asked this setup to be, in their own words, as "
                                         "one line of at most 40 characters. Not a description of the settings you "
                                         "changed. Send it whenever the change came from something the user asked for."}}}},
                       {"changes", "expectedSessionId", "expectedRevision"}),
         settings_output({{"applied", boolean_schema()}, {"changes", array_schema(change)}, {"normalized", array_schema(id)},
             {"processPresetDirty", boolean_schema()}, {"projectUndo", boolean_schema()}},
             {"applied", "changes", "normalized", "processPresetDirty", "projectUndo"}),
         ActionClass::Mutation, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::SettingsApplyPatch},
        {"intent_update", "Record what this print is for",
         "Record what the user said they want out of this print, as named answers you choose (only what they said or clearly implied, not your own plan; that belongs in plan_set): what it is for, how it will be used, what matters about it, how long it may take. Send question without value for something you have asked and do not know yet; the unanswered ones come back as openQuestions. Calling it shows the user an approval card in JusPrin and waits for their decision, because the card is where the user confirms you understood them. Project Undo does not undo this.",
         object_schema({{"fields", array_schema(object_schema({{"field", id}, {"value", text}, {"question", text},
                                                               {"assumed", boolean_schema()}}, {"field"}), 32)}},
                       {"fields"}),
         intent_output,
         ActionClass::Mutation, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::IntentUpdate},
        {"plan_set", "Pin your plan for this print",
         "State how you mean to print this project and why: the headline, one entry per decision with the alternative you rejected, what you assumed without being able to check, and what could still go wrong. Replaces the whole plan. Runs without an approval card because it records only your own words; project Undo does not undo this. It records your reasoning and groups nothing: to have the user approve several changes on one card, pass the same planId to each change tool; each returns queued, and after the last one end your turn and ask for the approval; workspace_inspect activities shows how they ended.",
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
         "Replace the open project: open a .3mf project, open a model file (.stl, .obj, .step, .amf, .drc) as a new project, or start an empty one with new. OrcaSlicer's questions become inputs: loadProjectSettings (project: use the file's printer, filament and process settings; keep: geometry only), unitConversion (keep; convertIfTiny: scale an object that looks modelled in metres or inches; inches: treat the model file as inches), oversized (keep, or scaleToFit the bed). Anything else OrcaSlicer would ask is answered with the choice that changes least and listed in decisions. If the open project has unsaved changes the call is refused unless unsavedWork is \"discard\", which the user must have agreed to. IDs from before are no longer valid afterwards. Calling it shows the user an approval card in JusPrin and waits for their decision, and the card shows the path.",
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
         "Save the open project to its own file, or to the absolute .3mf path you give, the way the user's Save does: the file becomes the project's file and the project is marked saved. Replaces whatever is at that path, so it waits for approval in JusPrin, and the card shows the exact path. A project that has never been saved needs a path. Only a .3mf project: G-code and other files are written with export_file.",
         object_schema({{"path", {{"type", "string"}, {"maxLength", 1024}}}}),
         object_schema({{"path", string_schema()}, {"saved", boolean_schema()}, {"projectDirty", boolean_schema()},
                        {"sessionId", id}, {"revision", revision}},
                       {"path", "saved", "projectDirty", "sessionId", "revision"}),
         ActionClass::Destructive, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::ProjectSave},
        {"history_restore", "Restore an earlier state",
         "Move the project through its undo history, as the person's own undo and redo lists do: to just before a step (it and every later step undone) or just after it (it and every earlier step done). Read step IDs from the history section of workspace_inspect. Setting edits, preset choices, the print intent, and the plan are not part of that history and stay as they are; the result lists which of them exist. Calling it shows the user an approval card in JusPrin and waits for their decision.",
         object_schema({{"sessionId", id}, {"stepId", id}, {"point", {{"type", "string"}, {"enum", json::array({"before", "after"})}}}},
                       {"sessionId", "stepId", "point"}),
         object_schema({{"history", history_section}, {"notReversed", list_schema(text)}, {"sessionId", id}, {"revision", revision}},
                       {"history", "notReversed", "sessionId", "revision"}),
         ActionClass::Destructive, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::HistoryRestore},
        {"printer_setup", "Set up the printer",
         "Establish the hardware for this job in OrcaSlicer's order: printer preset, plate type, process preset, then filament presets from the first slot. Names come from presets_list; plate types from printer_setup_preview's issues or the printer section. Preview first. If the switch would drop unsaved preset edits, the call is refused unless unsavedEdits is \"discard\", which the user must have agreed to. confirmFacts records what the user said about the physical printer that no sensor reports (for example fact \"plate\", value \"Textured PEI Plate\"; or \"bed_clear\"), each lasting hours (default 24). The result lists what OrcaSlicer replaced on its own. Calling it shows the user an approval card in JusPrin and waits for their decision.",
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
        {"object_import", "Import an attached model",
         // In-app only: the file reaches the app as a chat attachment.
         "Add the objects of a model file the user attached to this chat to the open project, in one undo step, on plateId if given. attachmentId comes from the attachment list. unitConversion (keep; convertIfTiny: if OrcaSlicer finds the model tiny, as if modelled in metres or inches, it converts it now and the decisions list says so; inches: the file is in inches) and oversized (keep, scaleToFit) answer OrcaSlicer's questions; anything else it asks is answered with the choice that changes least and listed in decisions. The result gives each new object's size, so check it before converting again. Calling it shows the user an approval card in JusPrin and waits for their decision.",
         object_schema({{"sessionId", id}, {"attachmentId", id}, {"plateId", id},
                        {"unitConversion", {{"type", "string"}, {"enum", json::array({"keep", "convertIfTiny", "inches"})}}},
                        {"oversized", {{"type", "string"}, {"enum", json::array({"keep", "scaleToFit"})}}}},
                       {"sessionId", "attachmentId"}),
         import_result,
         ActionClass::Mutation, ToolExposure::InApp, ToolAvailability::ImportableAttachment, ToolHandler::ObjectImport},
        {"object_import_file", "Import a model file",
         // MCP only: an external client names a file by path, shown on the card.
         "Add the objects of a model file (.stl, .obj, .step, .amf, .drc, or a .3mf's geometry) at an absolute path to the open project, in one undo step, on plateId if given. The file is read only after the user approves the card, which shows the path. unitConversion (keep; convertIfTiny: if OrcaSlicer finds the model tiny, as if modelled in metres or inches, it converts it now and the decisions list says so; inches: the file is in inches) and oversized (keep, scaleToFit) answer OrcaSlicer's questions; anything else it asks is answered with the choice that changes least and listed in decisions. The result gives each new object's size, so check it before converting again.",
         object_schema({{"sessionId", id}, {"path", {{"type", "string"}, {"maxLength", 1024}}}, {"plateId", id},
                        {"unitConversion", {{"type", "string"}, {"enum", json::array({"keep", "convertIfTiny", "inches"})}}},
                        {"oversized", {{"type", "string"}, {"enum", json::array({"keep", "scaleToFit"})}}}},
                       {"sessionId", "path"}),
         import_result,
         ActionClass::Mutation, ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::ObjectImportFile},
        {"project_delete_items", "Delete from the project",
         "Delete up to 32 items in one undo step: {objectId} for a whole object, {objectId, partId} for one part (part ids come from object_analyze mesh), {objectId, instance} for one copy, {plateId} for a plate, whose objects OrcaSlicer moves to another plate, or {regionId} for a region annotation and what it generated. The last solid part or copy of an object is refused; delete the object. Calling it shows the user an approval card in JusPrin and waits for their decision, and the card names each item.",
         object_schema({{"sessionId", id},
                        {"items", {{"type", "array"}, {"minItems", 1}, {"maxItems", 32},
                                   {"items", object_schema({{"objectId", id}, {"partId", id}, {"instance", integer_schema()},
                                                            {"plateId", id}, {"regionId", id}})}}}},
                       {"sessionId", "items"}),
         object_schema({{"objects", objects_section}, {"plateCount", integer_schema()},
                        {"removedRegions", {{"type", "array"}, {"items", id}, {"maxItems", 32}}},
                        {"sessionId", id}, {"revision", revision}},
                       {"objects", "plateCount", "sessionId", "revision"}),
         ActionClass::Destructive, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::ProjectDeleteItems},
        {"plate_layout", "Lay out the plates",
         "Lay out the job in one undo step: objects, up to 64 rows of {objectId, enabled (whether it prints), quantity (copies, 1 to 64), plateId (move it to that plate), name, extruder}; plates, up to 16 rows of {plateId?, name?, bedType?}, where a row without plateId adds a plate; arrange ({} for every unlocked plate, or {plateId}, with optional spacingMm and allowRotation), which runs as OrcaSlicer's arrange job: its state, and the objects no plate could hold, are read from workspace_inspect's slicing section under the returned handle. Arranging every plate also removes empty plates at the end, as OrcaSlicer does. Copies are placed next to the original; arrange them to spread them out. Calling it shows the user an approval card in JusPrin and waits for their decision.",
         object_schema({{"sessionId", id},
                        {"objects", {{"type", "array"}, {"minItems", 1}, {"maxItems", 64},
                                     {"items", object_schema({{"objectId", id}, {"enabled", boolean_schema()},
                                                              {"quantity", {{"type", "integer"}, {"minimum", 1}}},
                                                              {"plateId", id}, {"name", string_schema()},
                                                              {"extruder", {{"type", "integer"}, {"minimum", 1}}}},
                                                             {"objectId"})}}},
                        {"plates", {{"type", "array"}, {"minItems", 1}, {"maxItems", 16},
                                    {"items", object_schema({{"plateId", id}, {"name", string_schema()}, {"bedType", string_schema()}})}}},
                        {"arrange", object_schema({{"plateId", id}, {"spacingMm", number_schema()}, {"allowRotation", boolean_schema()}})}},
                       {"sessionId"}),
         object_schema({{"objects", objects_section}, {"addedPlateIds", {{"type", "array"}, {"items", id}, {"maxItems", 16}}},
                        {"handle", id}, {"sessionId", id}, {"revision", revision}},
                       {"objects", "addedPlateIds", "sessionId", "revision"}),
         ActionClass::Mutation, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::PlateLayout},
        {"object_place", "Place an object",
         "Place one copy of an object for printing, in one undo step. instance picks an existing copy (default 0, the first); to add copies use plate_layout quantity. Facets, all optional, applied in this order: unitsFix (inches or meters: the file's numbers are in those units, as object_analyze mesh unitsSuspicion reports; OrcaSlicer multiplies the size by 25.4 or 1000 and replaces the object, so the result names the new objectId; never scale by hand to fix units), scale (factors x, y, z) or scaleTo (a uniform scale that makes the size along one axis sizeMm), mirrorAxis, then at most one of faceDown (a face handle from object_analyze features, put on the bed), rotateDegrees (about the world x, y, z axes) or autoOrient (OrcaSlicer's own auto-orient for minimal support; it runs as a job whose state is read from workspace_inspect's slicing section under the returned handle), then position (x, y of the instance on the bed, mm) and dropToBed. To orient for another goal, score candidates with object_analyze orientations and place the chosen face down. Mirror and scale are named on the approval card. regionsUnbound lists region annotations whose geometry the change lost (a units fix rescales the mesh); moving or turning keeps them. Calling it shows the user an approval card in JusPrin and waits for their decision.",
         object_schema({{"sessionId", id}, {"objectId", id}, {"instance", integer_schema()},
                        {"unitsFix", {{"type", "string"}, {"enum", json::array({"inches", "meters"})}}},
                        {"scale", vector3},
                        {"scaleTo", object_schema({{"axis", {{"type", "string"}, {"enum", json::array({"x", "y", "z"})}}},
                                                   {"sizeMm", number_schema()}},
                                                  {"axis", "sizeMm"})},
                        {"mirrorAxis", {{"type", "string"}, {"enum", json::array({"x", "y", "z"})}}},
                        {"faceDown", id}, {"rotateDegrees", vector3},
                        {"position", {{"type", "array"}, {"items", number_schema()}, {"maxItems", 2}}},
                        {"dropToBed", {{"type", "boolean"}, {"enum", json::array({true})}}},
                        {"autoOrient", {{"type", "boolean"}, {"enum", json::array({true})}}}},
                       {"sessionId", "objectId"}),
         object_schema({{"objectId", id},
                        {"transform", object_schema({{"positionMm", vector3}, {"rotationDegrees", vector3}, {"scale", vector3}},
                                                    {"positionMm", "rotationDegrees", "scale"})},
                        {"sizeMm", vector3}, {"handle", id},
                        {"regionsUnbound", {{"type", "array"}, {"items", id}, {"maxItems", 32}}},
                        {"sessionId", id}, {"revision", revision}},
                       {"objectId", "transform", "sizeMm", "regionsUnbound", "sessionId", "revision"}),
         ActionClass::Mutation, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::ObjectPlace},
        {"view_render", "Picture a plate",
         "Picture the prepare view of one plate (default the active plate) from a named view: iso (default), front, rear, left, right, top, bottom, top_front, or plate (straight down on the whole plate). The plate's printable objects are drawn without the bed, at widthPx by heightPx (64 to 1280, default 1024 by 768). The picture comes back beside the result: as an image block over MCP, and as an image in JusPrin. Use it to check how a placement or a divide looks.",
         object_schema({{"plateId", id},
                        {"view", {{"type", "string"}, {"enum", json::array({"iso", "front", "rear", "left", "right", "top", "bottom", "top_front", "plate"})}}},
                        {"widthPx", {{"type", "integer"}, {"minimum", 64}, {"maximum", 1280}}},
                        {"heightPx", {{"type", "integer"}, {"minimum", 64}, {"maximum", 1280}}}}),
         object_schema({{"plateId", id}, {"view", id}, {"widthPx", integer_schema()}, {"heightPx", integer_schema()},
                        {"mimeType", id}, {"bytes", integer_schema()}, {"sessionId", id}, {"revision", revision}},
                       {"plateId", "view", "widthPx", "heightPx", "mimeType", "bytes", "sessionId", "revision"}),
         ActionClass::ReadOnly, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::ViewRender},
        {"project_attachment_read", "Read a project attachment",
         "Read one file packed in the project (its id from workspace_inspect's project section attachments): a picture comes back beside the result as an image (scaled to 1280 px and 2 MB when larger), text as text (up to 32 KB), and anything else as its name, type and size.",
         object_schema({{"id", id}}, {"id"}),
         object_schema({{"id", id}, {"folder", id}, {"kind", {{"type", "string"}, {"enum", json::array({"image", "text", "other"})}}},
                        {"mimeType", id}, {"bytes", integer_schema()}, {"text", {{"type", "string"}, {"maxLength", 32 * 1024}}},
                        {"widthPx", integer_schema()}, {"heightPx", integer_schema()}, {"truncated", boolean_schema()},
                        {"sessionId", id}, {"revision", revision}},
                       {"id", "folder", "kind", "mimeType", "bytes", "truncated", "sessionId", "revision"}),
         ActionClass::ReadOnly, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::AttachmentRead},
        {"slice_inspect", "Inspect a slice in detail",
         "Expert detail of a sliced plate (default the active plate). view layers: per layer, from first (default 0), up to count (1 to 100) layers: height, time, extrusion roles, speed, fan, temperature and volumetric flow ranges, with layerCount and the next first to ask for. view gcode: the G-code text from line first (0-based), up to 64 KB, with the next first. A plate that has not been sliced says so.",
         object_schema({{"plateId", id}, {"view", {{"type", "string"}, {"enum", json::array({"layers", "gcode"})}}},
                        {"first", integer_schema()}, {"count", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}},
                       {"view"}),
         object_schema({{"valid", boolean_schema()}, {"plateId", id}, {"view", id}, {"layerCount", integer_schema()},
                        {"layers", array_schema(object_schema({{"index", integer_schema()}, {"zMm", number_schema()},
                                                               {"heightMm", number_schema()}, {"seconds", number_schema()},
                                                               {"roles", array_schema(string_schema(), 16)},
                                                               {"speedMmS", before_after}, {"fanPercent", before_after},
                                                               {"temperatureC", before_after}, {"flowMm3S", before_after}},
                                                              {"index", "zMm", "heightMm", "seconds", "roles", "speedMmS", "fanPercent",
                                                               "temperatureC", "flowMm3S"}), 100)},
                        {"gcode", {{"type", "string"}, {"maxLength", 64 * 1024}}}, {"firstLine", integer_schema()},
                        {"next", integer_schema()}, {"sessionId", id}, {"revision", revision}},
                       {"valid", "plateId", "view", "sessionId", "revision"}),
         ActionClass::ReadOnly, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::SliceInspect},
        {"activity_cancel", "Cancel a run",
         "Stop something the tools started, by its handle: a slice (slice_start's handle), an arrange or orient job (plate_layout's or object_place's handle), or a call still waiting for approval (its action id). Says whether it stopped it; a job's end is read from workspace_inspect's slicing section. Runs without an approval card: the person can start any of these again.",
         object_schema({{"handle", id}}, {"handle"}),
         object_schema({{"handle", id}, {"kind", {{"type", "string"}, {"enum", json::array({"slice", "job", "proposal", "none"})}}},
                        {"cancelled", boolean_schema()}, {"message", string_schema()}, {"sessionId", id}, {"revision", revision}},
                       {"handle", "kind", "cancelled", "message", "sessionId", "revision"}),
         ActionClass::Mutation, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::ActivityCancel, true},
        {"export_file", "Export a file",
         "Write a file to an absolute path the user chose: gcode (a sliced plate's G-code, .gcode), sliced_3mf (a sliced plate with its G-code, .3mf), project_3mf (the whole project, .3mf, without changing which file the project is), stl (objectIds, or a plate's objects, as placed, .stl), or presets (the printer, filament and process presets in use, as files in the folder path). plateId picks the plate for gcode, sliced_3mf and stl (default the active plate). An existing file is replaced only with overwrite. The file is written only after approval: calling it shows the user an approval card in JusPrin with the exact path, the project's license when it restricts use, and the slice's own warnings for gcode and sliced_3mf (also in sliceWarnings), and waits for their decision. Check the slice with slice_report first and fix what it finds.",
         object_schema({{"sessionId", id},
                        {"kind", {{"type", "string"}, {"enum", json::array({"gcode", "sliced_3mf", "project_3mf", "stl", "presets"})}}},
                        {"path", id}, {"plateId", id},
                        {"objectIds", {{"type", "array"}, {"items", id}, {"minItems", 1}, {"maxItems", 64}}},
                        {"overwrite", boolean_schema()}},
                       {"sessionId", "kind", "path"}),
         object_schema({{"kind", id}, {"files", array_schema(string_schema(), 32)}, {"bytes", integer_schema()},
                        {"license", string_schema()}, {"licenseRestricted", boolean_schema()},
                        {"sliceWarnings", array_schema(string_schema(), 8)},
                        {"sessionId", id}, {"revision", revision}},
                       {"kind", "files", "bytes", "license", "licenseRestricted", "sessionId", "revision"}),
         ActionClass::Destructive, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::ExportFile},
        {"object_divide_preview", "Preview dividing an object",
         "Work out, without changing anything, what object_divide would make: each piece's size, volume, centre and overhang area (downward faces steeper than the support threshold, as the piece would lie: an estimate of what needs support), the overhang area before, and the region annotations that dividing would unbind. Same arguments as object_divide.",
         object_schema({{"sessionId", id}, {"objectId", id},
                        {"plane", object_schema({{"point", vector3}, {"normal", vector3},
                                                 {"keep", {{"type", "string"}, {"enum", json::array({"both", "upper", "lower"})}}},
                                                 {"asParts", boolean_schema()}},
                                                {"point", "normal"})},
                        {"shells", {{"type", "string"}, {"enum", json::array({"objects", "parts"})}}}},
                       {"sessionId", "objectId"}),
         object_schema({{"pieces", {{"type", "array"}, {"maxItems", 64}, {"items", piece_row}}},
                        {"overhangAreaBeforeMm2", number_schema()}, {"overhangAreaAfterMm2", number_schema()},
                        {"regionsUnbound", {{"type", "array"}, {"items", id}, {"maxItems", 32}}},
                        {"truncated", boolean_schema()}, {"sessionId", id}, {"revision", revision}},
                       {"pieces", "overhangAreaBeforeMm2", "overhangAreaAfterMm2", "regionsUnbound", "truncated", "sessionId", "revision"}),
         ActionClass::ReadOnly, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::ObjectDividePreview},
        {"object_divide", "Divide an object",
         "Divide one object in one undo step: plane {point, normal} in millimetres in the world frame as the object stands now, cuts it with OrcaSlicer's cut (the side the normal points to is upper; keep both, upper or lower; asParts keeps the pieces as parts of one object); or shells (objects or parts) splits it into its separate shells. The pieces are new objects with new ids, listed in the result; region annotations on the object are unbound (regionsUnbound). Preview first with object_divide_preview. Calling it shows the user an approval card in JusPrin describing the pieces, and waits for their decision.",
         object_schema({{"sessionId", id}, {"objectId", id},
                        {"plane", object_schema({{"point", vector3}, {"normal", vector3},
                                                 {"keep", {{"type", "string"}, {"enum", json::array({"both", "upper", "lower"})}}},
                                                 {"asParts", boolean_schema()}},
                                                {"point", "normal"})},
                        {"shells", {{"type", "string"}, {"enum", json::array({"objects", "parts"})}}}},
                       {"sessionId", "objectId"}),
         object_schema({{"pieces", {{"type", "array"}, {"maxItems", 64}, {"items", piece_row}}},
                        {"overhangAreaBeforeMm2", number_schema()}, {"overhangAreaAfterMm2", number_schema()},
                        {"regionsUnbound", {{"type", "array"}, {"items", id}, {"maxItems", 32}}},
                        {"truncated", boolean_schema()}, {"sessionId", id}, {"revision", revision}},
                       {"pieces", "overhangAreaBeforeMm2", "overhangAreaAfterMm2", "regionsUnbound", "truncated", "sessionId", "revision"}),
         ActionClass::Mutation, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::ObjectDivide},
        {"object_merge", "Merge objects",
         "Merge 2 to 16 objects into one object with each as a part, in one undo step, keeping where each is; the result is the new object's id, and region annotations on the merged objects are unbound. Calling it shows the user an approval card in JusPrin naming the objects, and waits for their decision.",
         object_schema({{"sessionId", id}, {"objectIds", {{"type", "array"}, {"items", id}, {"minItems", 2}, {"maxItems", 16}}}},
                       {"sessionId", "objectIds"}),
         object_schema({{"objectId", id}, {"regionsUnbound", {{"type", "array"}, {"items", id}, {"maxItems", 32}}},
                        {"sessionId", id}, {"revision", revision}},
                       {"objectId", "regionsUnbound", "sessionId", "revision"}),
         ActionClass::Mutation, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::ObjectMerge},
        {"object_repair", "Repair an object's mesh",
         "Repair the mesh of one object with OrcaSlicer's CGAL repair, in one undo step, when object_analyze mesh reports open edges; the result says what changed (open edges, facets, parts, volume, before and after). Paint on the object is cleared and region annotations whose geometry changed are listed as unbound. Nothing changes, and no undo step is added, when there are no open edges. Calling it shows the user an approval card in JusPrin and waits for their decision.",
         object_schema({{"sessionId", id}, {"objectId", id}}, {"sessionId", "objectId"}),
         object_schema({{"changed", boolean_schema()},
                        {"openEdges", before_after}, {"facets", before_after}, {"parts", before_after}, {"volumeMm3", before_after},
                        {"regionsUnbound", {{"type", "array"}, {"items", id}, {"maxItems", 32}}},
                        {"sessionId", id}, {"revision", revision}},
                       {"changed", "openEdges", "facets", "parts", "volumeMm3", "regionsUnbound", "sessionId", "revision"}),
         ActionClass::Mutation, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::ObjectRepair},
        {"region_annotate", "Mark what parts of an object mean",
         "Record what up to 32 regions of an object mean, and make OrcaSlicer print them that way. Each row is {objectId, kind, geometry} for a new region, or {regionId} to regenerate a stored one (adding objectId, kind or geometry replaces that part of it). kind says what the region is: seam_preferred or hidden puts the seam ON this face (use it for 'put the seam on the back'); seam_forbidden or visible keeps the seam OFF this face; smooth_face keeps both the seam and support marks off it; no_support keeps support off it, and precision_hole keeps support out of a hole (a blocker fills it); support_allowed asks for support there; reinforce and flexible (a modifier with settings, default wall_loops and sparse_infill_density; with geometry object, the object's own settings), material (extruder, 1-based). geometry: {type: face|hole, handle} from object_analyze features; {type: box, center, sizeMm} or {type: cylinder, center, axis, diameterMm, lengthMm} in millimetres in the world frame as the object stands now; {type: direction, vector, toleranceDegrees} for every face pointing that way now; {type: object}. The region stays with the object when it is moved or turned. Generates support and seam paint, support blockers and enforcers, modifier volumes or object settings in one undo step: project Undo removes those but not the annotation, which object_analyze then reports as artifactsMissing. Delete a region with project_delete_items {regionId}. Calling it shows the user an approval card in JusPrin listing each region and what it generates, and waits for their decision.",
         object_schema({{"sessionId", id},
                        {"regions", {{"type", "array"}, {"minItems", 1}, {"maxItems", 32},
                                     {"items", object_schema({{"regionId", id}, {"objectId", id},
                                                              {"kind", {{"type", "string"}, {"enum", json::array({"smooth_face", "no_support", "support_allowed", "precision_hole", "reinforce", "flexible", "visible", "hidden", "seam_preferred", "seam_forbidden", "material"})}}},
                                                              {"geometry", object_schema({{"type", {{"type", "string"}, {"enum", json::array({"face", "hole", "box", "cylinder", "direction", "object"})}}},
                                                                                          {"handle", id}, {"center", vector3}, {"sizeMm", vector3},
                                                                                          {"axis", vector3}, {"diameterMm", number_schema()},
                                                                                          {"lengthMm", number_schema()}, {"vector", vector3},
                                                                                          {"toleranceDegrees", number_schema()}},
                                                                                         {"type"})},
                                                              {"settings", {{"type", "object"}, {"maxProperties", 8},
                                                                            {"additionalProperties", {{"type", json::array({"string", "number", "boolean"})}}}}},
                                                              {"extruder", {{"type", "integer"}, {"minimum", 1}}}})}}}},
                       {"sessionId", "regions"}),
         object_schema({{"regions", {{"type", "array"}, {"maxItems", 32}, {"items", region_row}}},
                        {"projectUndo", boolean_schema()}, {"sessionId", id}, {"revision", revision}},
                       {"regions", "projectUndo", "sessionId", "revision"}),
         ActionClass::Mutation, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::RegionAnnotate},
        {"object_analyze", "Analyze an object",
         "Geometry facts for one object, by include: mesh (size, volume, facet and part counts, each part with its id and kind, open and repaired edges, and whether OrcaSlicer thinks it was modelled in inches or metres), features (the largest flat faces, each with a handle, area, sizeMm -- its two side lengths, longest first, which is how to find \"the 25 by 6 mm face\" -- centre and outward normal (0,0,-1 is the face on the bed), and hole mouths with a handle, diameter, centre and axis; millimetres in the world frame), fit (which plate holds each instance and whether it is inside it, objects whose footprint overlaps it, likely duplicates), orientations (OrcaSlicer's auto-orient cost for each of up to 8 candidates -- an up direction or a face handle to put down -- or for its own best candidates when none are given, with overhang and bed-contact area and the face handles that would rest on the bed; lower unprintability is better), measure (distance and angle between two feature handles, given as measure.from and measure.to), regions (the region annotations on this object, what each generated, and whether its geometry is still there -- bindingLost -- or project Undo or an edit removed what it generated -- artifactsMissing; region_annotate with the regionId regenerates it). Handles are valid until the project changes; afterwards a measure fails with feature_expired and the features must be read again.",
         object_schema({{"sessionId", id}, {"objectId", id},
                        {"include", {{"type", "array"}, {"minItems", 1}, {"maxItems", 6},
                                     {"items", {{"type", "string"}, {"enum", json::array({"mesh", "features", "fit", "orientations", "measure", "regions"})}}}}},
                        {"measure", object_schema({{"from", id}, {"to", id}}, {"from", "to"})},
                        {"candidates", {{"type", "array"}, {"minItems", 1}, {"maxItems", 8},
                                        {"items", object_schema({{"up", vector3}, {"faceDown", id}})}}}},
                       {"sessionId", "objectId", "include"}),
         object_schema({{"objectId", id},
                        {"mesh", object_schema({{"sizeMm", vector3}, {"volumeMm3", number_schema()}, {"facets", integer_schema()},
                                                {"parts", integer_schema()}, {"openEdges", integer_schema()},
                                                {"repaired", object_schema({{"edgesFixed", integer_schema()}, {"degenerateFacets", integer_schema()},
                                                                            {"facetsRemoved", integer_schema()}, {"facetsReversed", integer_schema()},
                                                                            {"backwardsEdges", integer_schema()}},
                                                                           {"edgesFixed", "degenerateFacets", "facetsRemoved", "facetsReversed", "backwardsEdges"})},
                                                {"unitsSuspicion", {{"type", "string"}, {"enum", json::array({"none", "inches", "meters"})}}},
                        {"partList", {{"type", "array"}, {"maxItems", 16},
                                      {"items", object_schema({{"partId", id}, {"name", string_schema()},
                                                               {"kind", {{"type", "string"}, {"enum", json::array({"model", "modifier", "negative", "enforcer", "blocker"})}}},
                                                               {"facets", integer_schema()}},
                                                              {"partId", "name", "kind", "facets"})}}}},
                                               {"sizeMm", "volumeMm3", "facets", "parts", "openEdges", "repaired", "unitsSuspicion"})},
                        {"features", object_schema({{"faces", {{"type", "array"}, {"maxItems", 32},
                                                              {"items", object_schema({{"handle", id}, {"areaMm2", number_schema()},
                                                                                       {"sizeMm", {{"type", "array"}, {"items", number_schema()}, {"maxItems", 2}}},
                                                                                       {"normal", vector3}, {"center", vector3}},
                                                                                      {"handle", "areaMm2", "sizeMm", "normal", "center"})}}},
                                                    {"holes", {{"type", "array"}, {"maxItems", 32},
                                                              {"items", object_schema({{"handle", id}, {"diameterMm", number_schema()},
                                                                                       {"center", vector3}, {"axis", vector3}},
                                                                                      {"handle", "diameterMm", "center", "axis"})}}},
                                                    {"truncated", boolean_schema()}},
                                                   {"faces", "holes", "truncated"})},
                        {"regions", object_schema({{"items", {{"type", "array"}, {"maxItems", 32}, {"items", region_row}}},
                                                   {"truncated", boolean_schema()}},
                                                  {"items", "truncated"})},
                        {"fit", object_schema({{"instances", {{"type", "array"}, {"maxItems", 16},
                                                              {"items", object_schema({{"instance", integer_schema()}, {"plateId", id},
                                                                                       {"inside", boolean_schema()}},
                                                                                      {"instance", "inside"})}}},
                                               {"overlaps", {{"type", "array"}, {"items", id}, {"maxItems", 16}}},
                                               {"likelyDuplicates", {{"type", "array"}, {"items", id}, {"maxItems", 16}}},
                                               {"truncated", boolean_schema()}},
                                              {"instances", "overlaps", "likelyDuplicates", "truncated"})},
                        {"orientations", {{"type", "array"}, {"maxItems", 8},
                                          {"items", object_schema({{"up", vector3}, {"unprintability", number_schema()},
                                                                   {"overhangArea", number_schema()}, {"bedContactMm2", number_schema()},
                                                                   {"facesDown", {{"type", "array"}, {"items", id}, {"maxItems", 8}}}},
                                                                  {"up", "unprintability", "overhangArea", "bedContactMm2", "facesDown"})}}},
                        {"measurement", object_schema({{"distanceMm", number_schema()}, {"planeDistanceMm", number_schema()},
                                                       {"angleDegrees", number_schema()}, {"deltaMm", vector3}})},
                        {"sessionId", id}, {"revision", revision}},
                       {"objectId", "sessionId", "revision"}),
         ActionClass::ReadOnly, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::ObjectAnalyze},
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
         "Read what a sliced plate says about itself, by section: summary is time and filament per extruder with weight and cost; findings are Orca's own warnings and errors (appliesWhen timelapse: only for a print that records a timelapse), the conflicts it detected, and whether a toolpath leaves the bed; material is filament and tool changes and the volume purged for them; supports lists support printed inside a hole of the mesh or inside a no_support or precision_hole region (with the support area summed over its layers); seams counts seams and how many landed on each face region that asks for or forbids them; firstLayer is the first layer height and each object's bed contact area and brim; islands lists slices with nothing of the object below them and whether support holds them; intent measures the slice against each print-intent answer that names a time, a weight or a cost (under five hours, less than 50 g, at most $2), and lists the answers it could not measure. A plate that has not been sliced says so rather than failing.",
         object_schema({{"plateId", id}, {"sections", {{"type", "array"},
                                                       {"items", {{"type", "string"},
                                                                  {"enum", json::array({"summary", "findings", "material", "supports", "seams", "firstLayer", "islands", "intent"})}}},
                                                       {"maxItems", 8}}}}),
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
                                                                                          {"appliesWhen", {{"type", "string"}, {"enum", json::array({"timelapse"})}}},
                                                                                          {"object", string_schema()}},
                                                                                         {"code", "message", "critical", "object"}), 32)},
                                                    {"conflict", text}, {"toolpathOutsideBed", boolean_schema()},
                                                    {"truncated", boolean_schema()}},
                                                   {"items", "conflict", "toolpathOutsideBed", "truncated"})},
                        {"material", object_schema({{"filamentChanges", integer_schema()}, {"extruderChanges", integer_schema()},
                                                    {"purgedMm3", number_schema()}, {"primeTowerMm3", number_schema()},
                                                    {"supportMm3", number_schema()}},
                                                   {"filamentChanges", "extruderChanges", "purgedMm3", "primeTowerMm3", "supportMm3"})},
                        {"supports", object_schema({{"generated", boolean_schema()}, {"layers", integer_schema()},
                                                    {"contacts", array_schema(object_schema({{"objectId", id}, {"object", string_schema()},
                                                                                             {"regionId", id}, {"target", string_schema()},
                                                                                             {"areaMm2", number_schema()}, {"layers", integer_schema()},
                                                                                             {"at", vector3}},
                                                                                            {"objectId", "object", "regionId", "target", "areaMm2", "layers", "at"}), 32)},
                                                    {"truncated", boolean_schema()}},
                                                   {"generated", "layers", "contacts", "truncated"})},
                        {"seams", object_schema({{"count", integer_schema()},
                                                 {"regions", array_schema(object_schema({{"regionId", id}, {"kind", id}, {"object", string_schema()},
                                                                                         {"seams", integer_schema()}},
                                                                                        {"regionId", "kind", "object", "seams"}), 32)},
                                                 {"truncated", boolean_schema()}},
                                                {"count", "regions", "truncated"})},
                        {"firstLayer", object_schema({{"heightMm", number_schema()},
                                                      {"objects", array_schema(object_schema({{"objectId", id}, {"object", string_schema()},
                                                                                              {"contactAreaMm2", number_schema()}, {"brim", boolean_schema()}},
                                                                                             {"objectId", "object", "contactAreaMm2", "brim"}), 32)},
                                                      {"truncated", boolean_schema()}},
                                                     {"heightMm", "objects", "truncated"})},
                        {"intent", object_schema({{"checks", array_schema(object_schema({{"field", string_schema()}, {"value", string_schema()},
                                                                                        {"kind", {{"type", "string"}, {"enum", json::array({"time", "weight", "cost"})}}},
                                                                                        {"limit", number_schema()}, {"actual", number_schema()},
                                                                                        {"within", boolean_schema()}},
                                                                                       {"field", "value", "kind", "limit", "actual", "within"}), 32)},
                                                  {"unchecked", array_schema(string_schema(), 32)}},
                                                 {"checks", "unchecked"})},
                        {"islands", object_schema({{"items", array_schema(object_schema({{"objectId", id}, {"object", string_schema()},
                                                                                         {"zMm", number_schema()}, {"areaMm2", number_schema()},
                                                                                         {"supported", boolean_schema()}, {"at", vector3}},
                                                                                        {"objectId", "object", "zMm", "areaMm2", "supported", "at"}), 32)},
                                                   {"truncated", boolean_schema()}},
                                                  {"items", "truncated"})},
                        {"sessionId", id}, {"revision", revision}},
                       {"valid", "plateId", "sessionId", "revision"}),
         ActionClass::ReadOnly, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::SliceReportRead},
        {"slice_start", "Slice the plate",
         "Start Orca's own slicing run for one plate, or every plate when you name none, and return once it has started; read the slicing section of workspace_inspect for the result, which is not ready then. With wait, return when the run ends instead (finished says whether the plates are sliced), which is what to use before reading slice_report. Fails when a slice is already running unless you pass preempt, because nothing records who started that run and it may be the user's. Runs without an approval card unless it preempts.",
         object_schema({{"plateId", id}, {"preempt", boolean_schema()}, {"wait", boolean_schema()}}),
         object_schema({{"handle", id}, {"started", boolean_schema()}, {"finished", boolean_schema()}, {"slicing", slicing_section},
                        {"sessionId", id}, {"revision", revision}},
                       {"handle", "started", "slicing", "sessionId", "revision"}),
         ActionClass::Mutation, ToolExposure::InApp | ToolExposure::Mcp, ToolAvailability::Always, ToolHandler::SliceStart,
         true},
        {"workspace_inspect",
         "Inspect the live workspace",
         "Read the open project. The default summary covers plates and objects, setup names, selection IDs, and whether undo and redo are possible. Other sections: objects (every object with its plates, instance, part and modifier counts, printable flag, extruder, size, and override count), project (the file's path and saved state, its own description, designer, license and copyright, packed attachments, and backup state), intent (what this print is for), plan (the plan in force), slicing (whether each plate's slice is current, and a slice in flight), history (the undo steps, for history_restore), activities (the latest tool calls with their state, plan and error, to learn how an approved plan ended), printer (the selected printer as configured and as the machine reports it, facts the user confirmed, and where they disagree). IDs are strings scoped to the returned sessionId. No process-setting values are exposed by this tool.",
         object_schema({{"sections", {{"type", "array"},
                                      {"items", {{"type", "string"},
                                                 {"enum", json::array({"summary", "project", "objects", "intent", "plan", "slicing", "history", "printer", "activities"})}}},
                                      {"maxItems", 9}}}}),
         object_schema({{"intent", intent_section}, {"plan", plan_section}, {"slicing", slicing_section},
                        {"printer", printer_section}, {"project", project_section}, {"objects", objects_section}, {"sessionId", id}, {"revision", revision}, {"projectName", string_schema()},
                         {"projectDirty", boolean_schema()}, {"printerPreset", string_schema()},
                         {"filamentPreset", string_schema()}, {"activePlateId", id},
                         {"plateCount", revision}, {"objectCount", revision}, {"plates", list_schema(plate_summary)},
                         {"selection", selection_summary}, {"truncated", boolean_schema()},
                         {"history", history_section},
                         {"activities", array_schema(object_schema({{"actionId", string_schema()}, {"tool", string_schema()}, {"title", string_schema()},
                                                                   {"state", string_schema()}, {"planId", string_schema()},
                                                                   {"error", object_schema({{"code", string_schema()}, {"message", string_schema()}}, json::array({"code", "message"}))}},
                                                                  json::array({"actionId", "tool", "title", "state"})), 20)}},
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
        // The printer panel's own two, one per mode. They exist only inside a
        // printer session, which is why they carry no in-app or MCP exposure:
        // the project conversation has no printer to identify or change.
        // Neither decides anything: the model does, and these check its
        // decision against the printer data and carry it out.
        {"printer_identify",
         "Show the printers you mean",
         "You decide which printer this is. This tool checks the printers you name against the app's printer list, shows them to "
         "the person, and returns the details to mention. It never picks a printer.",
         object_schema(json{{"catalogIds", {{"type", "array"}, {"items", string_schema()},
                                            {"description", "1 to 3 ids, copied exactly from the printer list."}}},
                            {"nozzle", {{"type", "number"},
                                        {"description", "Nozzle size in mm, only when the person or a photo said it."}}}},
                       json::array({"catalogIds"})),
         object_schema(json{{"printers",
                             {{"type", "array"}, {"maxItems", 3},
                              {"items", object_schema(json{{"catalogId", string_schema()},
                                                           {"brand", string_schema()},
                                                           {"model", string_schema()},
                                                           {"buildVolume", string_schema()},
                                                           {"nozzles", {{"type", "array"}, {"items", number_schema()}}},
                                                           {"assumed", object_schema(json{{"nozzle", number_schema()},
                                                                                          {"plate", {{"type", json::array({"string", "null"})}}},
                                                                                          {"filament", {{"type", json::array({"string", "null"})}}}},
                                                                                     json::array({"nozzle", "plate", "filament"}))},
                                                           {"alreadyYours", boolean_schema()}},
                                                      json::array({"catalogId", "brand", "model", "buildVolume", "nozzles",
                                                                   "assumed", "alreadyYours"}))}}}},
                       json::array({"printers"})),
         ActionClass::ReadOnly,
         ToolExposure::Printer,
         ToolAvailability::Always,
         ToolHandler::PrinterIdentify},
        {"printer_change",
         "Change this printer",
         "You work out what changed on this printer from what the person says. This tool checks it against what this printer's "
         "profile allows, asks the person to confirm, saves it, and returns the printer as it now is. It never decides what changed.",
         object_schema(json{{"nozzle", {{"type", "number"}, {"description", "The nozzle size now on it, in mm."}}},
                            {"spools", {{"type", "array"}, {"maxItems", 16},
                                        {"description", "The complete list of spools loaded now; it replaces the whole list."},
                                        {"items", object_schema(json{{"name", {{"type", "string"}, {"maxLength", 64}}},
                                                                     {"material", {{"type", "string"}, {"maxLength", 32}}},
                                                                     {"colour", {{"type", "string"}, {"maxLength", 9}}}},
                                                                json::array({"name", "material"}))}}}}),
         object_schema(json{{"state", {{"type", "string"}, {"enum", json::array({"applied", "declined"})}}},
                            {"changed", {{"type", "array"},
                                         {"items", object_schema(json{{"field", {{"type", "string"}, {"enum", json::array({"nozzle", "spools"})}}},
                                                                      {"before", {{"type", json::array({"number", "array"})}, {"items", printer_spool_schema()}}},
                                                                      {"after", {{"type", json::array({"number", "array"})}, {"items", printer_spool_schema()}}}},
                                                                 json::array({"field", "before", "after"}))}}},
                            {"printer", object_schema(json{{"name", string_schema()},
                                                           {"nozzle", number_schema()},
                                                           {"nozzles", {{"type", "array"}, {"items", number_schema()}}},
                                                           {"spools", {{"type", "array"}, {"items", printer_spool_schema()}}},
                                                           {"connected", boolean_schema()}},
                                                      json::array({"name", "nozzle", "nozzles", "spools", "connected"}))}},
                       json::array({"state", "changed", "printer"})),
         ActionClass::Mutation,
         ToolExposure::Printer,
         ToolAvailability::Always,
         ToolHandler::PrinterChange},
    };
    // Every change can join a plan; the decoders never see the field.
    // plan_set records the agent's own words and is not a change to group.
    for (ToolDefinition& definition : definitions)
        if (joins_plans(definition))
            definition.input_schema["properties"]["planId"] = {
                {"type", "string"}, {"maxLength", 64},
                {"description", "Only to group two or more changes; leave it out for a single change. Calls with the same planId "
                                "share one approval card and run in order; each returns queued, and you do not hear how they end."}};
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
                                                 "maxLength", "description"};
    for (const auto& item : schema.items())
        if (!supported.count(item.key())) throw std::logic_error("Unsupported canonical tool schema keyword: " + item.key());
    // A closed vocabulary constrains the value itself whatever its type, so it
    // is checked before the type dispatch: a provenance word, an action state,
    // a section name.
    if (const auto allowed = schema.find("enum");
        allowed != schema.end() &&
        std::none_of(allowed->begin(), allowed->end(), [&value](const json& candidate) { return candidate == value; }))
        return false;
    if (schema.at("type").is_array()) {
        for (const json& name : schema.at("type")) {
            json one    = schema;
            one["type"] = name;
            if (name != "array")
                one.erase("items");
            if (name == "null" ? value.is_null() : matches_schema(value, one))
                return true;
        }
        return false;
    }
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
    // A plan id belongs to the coordinator, not the tool: checked here, kept
    // out of the tool's own decoder, and put back for the proposal to read.
    std::optional<json> plan_id;
    if (arguments.is_object() && arguments.contains("planId")) {
        plan_id = arguments["planId"];
        arguments.erase("planId");
        if (!joins_plans(definition) || !plan_id->is_string() || plan_id->get_ref<const std::string&>().empty() ||
            plan_id->get_ref<const std::string&>().size() > 64)
            return {{}, ToolError{"invalid_arguments", !joins_plans(definition) ?
                                                           definition.name + " does not join a plan; only changes to the project take a planId." :
                                                           "planId must be a string of 1 to 64 characters."}};
    }
    if (arguments.is_discarded() || !valid_arguments(definition, arguments)) {
        // Name what is plainly wrong at the top level; the rest is in the
        // schema the caller already has.
        std::string message = "The tool arguments do not match the registered contract.";
        const json properties = definition.input_schema.value("properties", json::object());
        if (arguments.is_object()) {
            std::string unexpected, missing;
            for (const auto& item : arguments.items())
                if (!properties.contains(item.key()))
                    unexpected += (unexpected.empty() ? "" : ", ") + item.key();
            for (const json& key : definition.input_schema.value("required", json::array()))
                if (!arguments.contains(key.get<std::string>()))
                    missing += (missing.empty() ? "" : ", ") + key.get<std::string>();
            std::string outside;
            for (const auto& item : arguments.items()) {
                const auto property = properties.find(item.key());
                if (property != properties.end() && property->contains("enum") &&
                    std::find((*property)["enum"].begin(), (*property)["enum"].end(), item.value()) == (*property)["enum"].end())
                    outside += (outside.empty() ? "" : "; ") + item.key() + " must be one of " + (*property)["enum"].dump();
            }
            if (!unexpected.empty())
                message += " Not a parameter of this tool: " + unexpected + ".";
            if (!missing.empty())
                message += " Missing: " + missing + ".";
            if (!outside.empty())
                message += " " + outside + ".";
            if (definition.handler == ToolHandler::ProjectOpen && arguments.contains("path") == arguments.contains("new"))
                message += " Give exactly one of path and new.";
        }
        return {{}, ToolError{"invalid_arguments", message}};
    }
    const auto canonical = [](json& values) {
        for (auto& value : values)
            if (!value.is_string()) value = value.is_boolean() ? (value.get<bool>() ? "1" : "0") : value.dump();
    };
    if (definition.handler == ToolHandler::SettingsPreviewPatch || definition.handler == ToolHandler::SettingsApplyPatch)
        canonical(arguments["changes"]);
    if (definition.handler == ToolHandler::RegionAnnotate)
        for (auto& row : arguments["regions"])
            if (row.contains("settings"))
                canonical(row["settings"]);
    if (plan_id)
        arguments["planId"] = *plan_id;
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
