#pragma once

// The region vocabulary shared by the tools, the adapters and the project
// store: which kinds exist, which geometry each accepts, what each generates,
// how a record is worded, and how it is stored. GUI-free.

#include "Workspace.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Workspace {

struct RegionKindInfo
{
    const char*              name;
    const char*              words;      // how a card says it
    std::vector<std::string> geometries; // the geometry types it accepts
};

inline const std::vector<RegionKindInfo>& region_kinds()
{
    static const std::vector<RegionKindInfo> kinds{
        {"smooth_face", "smooth face", {"face", "direction"}},
        {"no_support", "no support", {"face", "hole", "box", "cylinder", "direction"}},
        {"support_allowed", "support allowed", {"face", "box", "cylinder", "direction"}},
        {"precision_hole", "precision hole", {"hole"}},
        {"reinforce", "reinforce", {"hole", "box", "cylinder", "object"}},
        {"flexible", "flexible", {"box", "cylinder", "object"}},
        {"visible", "visible", {"face", "direction"}},
        {"hidden", "hidden", {"face", "direction"}},
        {"seam_preferred", "seam here", {"face", "direction"}},
        {"seam_forbidden", "no seam here", {"face", "direction"}},
        {"material", "material", {"face", "box", "cylinder", "direction"}},
    };
    return kinds;
}

inline const RegionKindInfo* region_kind(const std::string& name)
{
    for (const RegionKindInfo& kind : region_kinds())
        if (name == kind.name)
            return &kind;
    return nullptr;
}

inline bool region_kind_accepts(const std::string& kind, const std::string& geometry)
{
    const RegionKindInfo* info = region_kind(kind);
    return info && std::find(info->geometries.begin(), info->geometries.end(), geometry) != info->geometries.end();
}

// The settings a kind writes when the request names none. Reinforcing adds
// walls and infill; flexible thins them. Values are relative to nothing: they
// are what the region prints with.
inline std::map<std::string, std::string> region_default_settings(const std::string& kind)
{
    if (kind == "reinforce")
        return {{"wall_loops", "5"}, {"sparse_infill_density", "40%"}};
    if (kind == "flexible")
        return {{"wall_loops", "1"}, {"sparse_infill_density", "5%"}};
    return {};
}

inline std::string region_number(double value)
{
    char text[32];
    std::snprintf(text, sizeof(text), std::abs(value) >= 100 ? "%.0f" : "%.3g", value);
    return text;
}

inline std::string region_geometry_words(const RegionGeometry& geometry)
{
    if (geometry.type == "face")
        return region_number(geometry.area) + " mm2 face";
    if (geometry.type == "hole")
        return "hole of " + region_number(geometry.diameter) + " mm";
    if (geometry.type == "box")
        return region_number(geometry.size[0]) + " x " + region_number(geometry.size[1]) + " x " + region_number(geometry.size[2]) + " mm box";
    if (geometry.type == "cylinder")
        return region_number(geometry.diameter) + " x " + region_number(geometry.length) + " mm cylinder";
    if (geometry.type == "direction")
        return "faces within " + region_number(geometry.tolerance_degrees) + " degrees of (" + region_number(geometry.normal[0]) + ", " +
               region_number(geometry.normal[1]) + ", " + region_number(geometry.normal[2]) + ")";
    return "the whole object";
}

inline std::string region_artifact_words(const RegionArtifact& artifact)
{
    if (artifact.type == "volume") {
        std::string words = artifact.target == "support_blocker"  ? "support blocker" :
                            artifact.target == "support_enforcer" ? "support enforcer" : "modifier";
        return words + " volume";
    }
    if (artifact.type == "paint")
        return artifact.target + " " + artifact.state + " paint on " + std::to_string(artifact.facets.size()) + " facets";
    return "object setting " + artifact.target + " = " + artifact.state;
}

inline std::string region_label(const RegionRecord& record)
{
    const RegionKindInfo* info = region_kind(record.kind);
    std::string label = std::string(info ? info->words : record.kind) + ", " + region_geometry_words(record.geometry) + " on " +
                        record.object_name;
    std::vector<std::string> made;
    for (const RegionArtifact& artifact : record.artifacts) {
        std::string words = artifact.type == "paint" ? artifact.target + " " + artifact.state + " paint" : region_artifact_words(artifact);
        if (std::find(made.begin(), made.end(), words) == made.end())
            made.push_back(words);
    }
    for (std::size_t index = 0; index < made.size(); ++index)
        label += (index == 0 ? ": " : ", ") + made[index];
    return label;
}

inline std::string region_volume_name(const RegionRecord& record, const std::string& target)
{
    return "JusPrin " + record.id + " " + (target == "support_blocker" ? "support blocker" : target == "support_enforcer" ? "support enforcer" : "modifier");
}

// What a kind generates for a geometry, before facets are resolved.
inline std::vector<RegionArtifact> region_artifact_plan(const RegionRecord& record)
{
    const std::string& kind = record.kind;
    const std::string& type = record.geometry.type;
    const bool painted = type == "face" || type == "direction";
    std::vector<RegionArtifact> plan;
    const auto paint = [&](const char* target, std::string state) {
        plan.push_back({"paint", target, std::move(state), {}, record.part, {}});
    };
    const auto volume = [&](const char* target) { plan.push_back({"volume", target, {}, region_volume_name(record, target), 0, {}}); };
    if (kind == "smooth_face") {
        paint("support", "blocker");
        paint("seam", "blocker");
    } else if (kind == "no_support" || kind == "precision_hole") {
        painted ? paint("support", "blocker") : volume("support_blocker");
    } else if (kind == "support_allowed") {
        painted ? paint("support", "enforcer") : volume("support_enforcer");
    } else if (kind == "visible" || kind == "seam_forbidden") {
        paint("seam", "blocker");
    } else if (kind == "hidden" || kind == "seam_preferred") {
        paint("seam", "enforcer");
    } else if (kind == "material") {
        painted ? paint("color", std::to_string(record.extruder)) : volume("modifier");
    } else if (type == "object") { // reinforce, flexible
        for (const auto& [key, value] : record.settings)
            plan.push_back({"override", key, value, {}, 0, {}});
    } else {
        volume("modifier");
    }
    return plan;
}

inline nlohmann::json region_vec3_json(const Vec3& value) { return nlohmann::json::array({value[0], value[1], value[2]}); }

inline Vec3 region_vec3_from(const nlohmann::json& value)
{
    Vec3 result{0, 0, 0};
    if (value.is_array() && value.size() == 3)
        for (std::size_t index = 0; index < 3; ++index)
            if (value[index].is_number())
                result[index] = value[index].get<double>();
    return result;
}

// The stored form. Readers take what they understand and default the rest, so
// a record written by a newer build loads.
inline nlohmann::json region_record_json(const RegionRecord& record)
{
    using nlohmann::json;
    json artifacts = json::array();
    for (const RegionArtifact& artifact : record.artifacts)
        artifacts.push_back({{"type", artifact.type}, {"target", artifact.target}, {"state", artifact.state},
                             {"name", artifact.name}, {"part", artifact.part}, {"facets", artifact.facets}});
    const RegionGeometry& g = record.geometry;
    return {{"regionId", record.id},
            {"kind", record.kind},
            {"label", record.label},
            {"object", {{"session", record.session}, {"id", record.object}, {"name", record.object_name},
                        {"partFacets", record.part_facets}, {"part", record.part}}},
            {"geometry", {{"type", g.type}, {"center", region_vec3_json(g.center)}, {"normal", region_vec3_json(g.normal)},
                          {"size", region_vec3_json(g.size)}, {"area", g.area}, {"diameter", g.diameter}, {"length", g.length},
                          {"toleranceDegrees", g.tolerance_degrees}}},
            {"settings", record.settings},
            {"extruder", record.extruder},
            {"artifacts", std::move(artifacts)},
            {"provenance", record.provenance},
            {"seq", record.seq},
            {"updatedAt", record.updated_at}};
}

inline RegionRecord region_record_from(const nlohmann::json& value)
{
    RegionRecord record;
    if (!value.is_object())
        return record;
    const auto text = [](const nlohmann::json& object, const char* key) {
        return object.contains(key) && object[key].is_string() ? object[key].get<std::string>() : std::string();
    };
    const auto number = [](const nlohmann::json& object, const char* key) {
        return object.contains(key) && object[key].is_number() ? object[key].get<double>() : 0.0;
    };
    const auto whole = [](const nlohmann::json& object, const char* key) {
        return object.contains(key) && object[key].is_number_unsigned() ? object[key].get<std::uint64_t>() : std::uint64_t{0};
    };
    record.id         = text(value, "regionId");
    record.kind       = text(value, "kind");
    record.label      = text(value, "label");
    record.provenance = text(value, "provenance");
    record.updated_at = text(value, "updatedAt");
    record.seq        = whole(value, "seq");
    if (value.contains("object") && value["object"].is_object()) {
        const nlohmann::json& object = value["object"];
        record.session     = whole(object, "session");
        record.object      = whole(object, "id");
        record.object_name = text(object, "name");
        record.part        = static_cast<std::size_t>(whole(object, "part"));
        if (object.contains("partFacets") && object["partFacets"].is_array())
            for (const nlohmann::json& count : object["partFacets"])
                if (count.is_number_unsigned())
                    record.part_facets.push_back(count.get<std::size_t>());
    }
    if (value.contains("geometry") && value["geometry"].is_object()) {
        const nlohmann::json& g = value["geometry"];
        record.geometry.type              = text(g, "type");
        record.geometry.center            = region_vec3_from(g.value("center", nlohmann::json()));
        record.geometry.normal            = region_vec3_from(g.value("normal", nlohmann::json()));
        record.geometry.size              = region_vec3_from(g.value("size", nlohmann::json()));
        record.geometry.area              = number(g, "area");
        record.geometry.diameter          = number(g, "diameter");
        record.geometry.length            = number(g, "length");
        record.geometry.tolerance_degrees = number(g, "toleranceDegrees");
    }
    if (value.contains("settings") && value["settings"].is_object())
        for (const auto& [key, setting] : value["settings"].items())
            if (setting.is_string())
                record.settings[key] = setting.get<std::string>();
    if (value.contains("extruder") && value["extruder"].is_number_integer())
        record.extruder = value["extruder"].get<int>();
    if (value.contains("artifacts") && value["artifacts"].is_array())
        for (const nlohmann::json& item : value["artifacts"]) {
            if (!item.is_object())
                continue;
            RegionArtifact artifact;
            artifact.type   = text(item, "type");
            artifact.target = text(item, "target");
            artifact.state  = text(item, "state");
            artifact.name   = text(item, "name");
            artifact.part   = static_cast<std::size_t>(whole(item, "part"));
            if (item.contains("facets") && item["facets"].is_array())
                for (const nlohmann::json& facet : item["facets"])
                    if (facet.is_number_integer())
                        artifact.facets.push_back(facet.get<int>());
            record.artifacts.push_back(std::move(artifact));
        }
    if (record.provenance.empty())
        record.provenance = "user_confirmed";
    return record;
}

// The next unused id: r1, r2, ... never reusing one still stored.
inline std::string next_region_id(const std::vector<RegionRecord>& stored, const std::vector<RegionRecord>& planned)
{
    unsigned highest = 0;
    for (const auto* list : {&stored, &planned})
        for (const RegionRecord& record : *list)
            if (record.id.size() > 1 && record.id[0] == 'r')
                highest = std::max<unsigned>(highest, static_cast<unsigned>(std::strtoul(record.id.c_str() + 1, nullptr, 10)));
    return "r" + std::to_string(highest + 1);
}

} // namespace Slic3r::GUI::JusPrin::Workspace
