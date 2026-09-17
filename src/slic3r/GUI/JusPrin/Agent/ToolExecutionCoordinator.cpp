#include "ToolExecutionCoordinator.hpp"
#include "Base64.hpp"
#include "IntentChecks.hpp"
#include "ToolResults.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SettingsSupport.hpp"
#include "slic3r/GUI/JusPrin/Workspace/UtcTime.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <set>
#include <sstream>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

using nlohmann::json;
using Workspace::WorkspaceChangeReasons;
using Workspace::WorkspaceError;

constexpr const char* kServerName = "jusprin-native";

// Workspace changes that can invalidate a pending proposal. Selection-only
// changes do not: a proposal pins its target by ID, so what the user has
// selected afterwards cannot alter what an approval would execute.
constexpr WorkspaceChangeReasons kInvalidatingReasons = WorkspaceChangeReasons::Contents | WorkspaceChangeReasons::Transform |
                                                        WorkspaceChangeReasons::Plates | WorkspaceChangeReasons::History |
                                                        WorkspaceChangeReasons::Project | WorkspaceChangeReasons::Settings;

// A settings patch is bound to the values it previewed, which the apply
// checks again; only a settings edit or a new project makes it stale. Model
// edits, and the slicing and selection events that follow them, do not.
constexpr WorkspaceChangeReasons kSettingsReasons = WorkspaceChangeReasons::Settings | WorkspaceChangeReasons::Project;

bool settings_patch_tool(const std::string& tool) { return tool == "settings_apply_patch"; }

// Whether a settings patch's preview still holds: same session, and no
// settings change after the revision it was previewed at.
bool settings_revision_holds(const json& arguments, const Workspace::WorkspaceSnapshot& snapshot, std::uint64_t settings_revision)
{
    return arguments.at("expectedSessionId") == std::to_string(snapshot.session.value()) &&
           arguments.at("expectedRevision").is_number_unsigned() &&
           arguments.at("expectedRevision").get<std::uint64_t>() <= snapshot.revision &&
           arguments.at("expectedRevision").get<std::uint64_t>() >= settings_revision;
}

const char* workspace_error_code(WorkspaceError error)
{
    switch (error) {
    case WorkspaceError::None: return "none";
    case WorkspaceError::InvalidId: return "invalid_id";
    case WorkspaceError::MissingObject: return "missing_object";
    case WorkspaceError::StaleId: return "stale_id";
    case WorkspaceError::UnsupportedSelection: return "unsupported_selection";
    case WorkspaceError::UnavailableOperation: return "unavailable_operation";
    case WorkspaceError::InvalidArgument: return "invalid_argument";
    case WorkspaceError::NoChange: return "no_change";
    case WorkspaceError::InvalidSettings: return "invalid_settings";
    case WorkspaceError::StaleSettings: return "stale_workspace";
    case WorkspaceError::FeatureExpired: return "feature_expired";
    }
    return "unknown";
}

std::optional<Workspace::ObjectId> parse_object_argument(const std::string& arguments_json)
{
    const json arguments = json::parse(arguments_json, nullptr, false);
    if (!arguments.is_object() || !arguments.contains("sessionId") || !arguments["sessionId"].is_string() ||
        !arguments.contains("objectId") || !arguments["objectId"].is_string())
        return std::nullopt;
    try {
        const std::uint64_t session = std::stoull(arguments["sessionId"].get<std::string>());
        const std::uint64_t value   = std::stoull(arguments["objectId"].get<std::string>());
        return Workspace::ObjectId(Workspace::ProjectSessionId(session), value);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// A license that limits what may be done with the prints or the files.
bool license_restricts(const std::string& license)
{
    std::string lower = license;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    for (char& c : lower)
        if (c == '_' || c == ' ')
            c = '-';
    lower = "-" + lower + "-";
    for (const char* mark : {"-nc-", "-nd-", "noncommercial", "non-commercial", "noderivatives", "no-derivatives",
                             "all-rights-reserved", "personal-use"})
        if (lower.find(mark) != std::string::npos)
            return true;
    return false;
}

Workspace::ExportRequest export_request(const json& arguments)
{
    const Workspace::ProjectSessionId session(std::stoull(arguments["sessionId"].get<std::string>()));
    Workspace::ExportRequest request;
    request.kind      = arguments["kind"];
    request.path      = arguments["path"];
    request.overwrite = arguments.value("overwrite", false);
    if (arguments.contains("plateId"))
        request.plate = Workspace::PlateId(session, std::stoull(arguments["plateId"].get<std::string>()));
    for (const json& id : arguments.value("objectIds", json::array()))
        request.objects.emplace_back(session, std::stoull(id.get<std::string>()));
    return request;
}

// What the slice being exported says is wrong with it, as the findings
// section words it: Orca's warnings that apply to every print, a conflict, a
// toolpath off the bed.
std::vector<std::string> export_slice_warnings(Workspace::IWorkspace& workspace, const Workspace::ExportRequest& request)
{
    std::vector<std::string> warnings;
    if (request.kind != "gcode" && request.kind != "sliced_3mf")
        return warnings;
    const auto plate = request.plate ? request.plate : workspace.snapshot().active_plate;
    if (!plate)
        return warnings;
    const Workspace::SliceReport report = workspace.slice_report(*plate, {});
    if (!report.valid)
        return warnings;
    for (const Workspace::SliceFinding& finding : report.findings)
        if (finding.applies_when.empty())
            warnings.push_back(finding.message);
    if (!report.conflict.empty())
        warnings.push_back(report.conflict);
    if (report.toolpath_outside)
        warnings.push_back("A toolpath leaves the printable area.");
    return warnings;
}

Workspace::DivideRequest divide_request(const json& arguments)
{
    Workspace::DivideRequest request;
    if (arguments.contains("shells")) {
        request.mode     = Workspace::DivideRequest::Mode::Shells;
        request.as_parts = arguments["shells"] == "parts";
        return request;
    }
    const json& plane = arguments["plane"];
    for (int axis = 0; axis < 3; ++axis) {
        request.point[axis]  = plane["point"][axis].get<double>();
        request.normal[axis] = plane["normal"][axis].get<double>();
    }
    const std::string keep = plane.value("keep", "both");
    request.keep_upper     = keep != "lower";
    request.keep_lower     = keep != "upper";
    request.as_parts       = plane.value("asParts", false);
    return request;
}

// Millimetres to a hundredth, as the cards and results show them.
json rounded_vec(const Workspace::Vec3& v)
{
    return json::array({std::round(v[0] * 100) / 100 + 0.0, std::round(v[1] * 100) / 100 + 0.0, std::round(v[2] * 100) / 100 + 0.0});
}

std::string number_words(double value)
{
    std::ostringstream out;
    out << std::round(value * 100) / 100 + 0.0;
    return out.str();
}

json divide_result(const Workspace::DivideResult& divided, json regions_unbound, const Workspace::WorkspaceSnapshot& snapshot)
{
    json   pieces = json::array();
    double after  = 0;
    for (const Workspace::DividedPiece& piece : divided.pieces) {
        after += piece.overhang_area;
        if (pieces.size() == 64)
            continue;
        json row{{"name", piece.name.substr(0, kToolTextLimit)}, {"volumeMm3", std::round(piece.volume * 100) / 100},
                 {"sizeMm", rounded_vec(piece.size)}, {"centerMm", rounded_vec(piece.center)},
                 {"overhangAreaMm2", std::round(piece.overhang_area * 100) / 100}};
        if (piece.object)
            row["objectId"] = std::to_string(piece.object->value());
        pieces.push_back(std::move(row));
    }
    return {{"pieces", std::move(pieces)}, {"overhangAreaBeforeMm2", std::round(divided.overhang_area_before * 100) / 100},
            {"overhangAreaAfterMm2", std::round(after * 100) / 100}, {"regionsUnbound", std::move(regions_unbound)},
            {"truncated", divided.pieces.size() > 64},
            {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}};
}

std::vector<Workspace::RegionRequest> region_requests(const json& arguments)
{
    const Workspace::ProjectSessionId     session(std::stoull(arguments["sessionId"].get<std::string>()));
    std::vector<Workspace::RegionRequest> requests;
    const auto vec = [](const json& value) {
        return Workspace::Vec3{value[0].get<double>(), value[1].get<double>(), value[2].get<double>()};
    };
    for (const json& row : arguments["regions"]) {
        Workspace::RegionRequest request;
        request.region_id = row.value("regionId", "");
        if (row.contains("objectId"))
            request.object = Workspace::ObjectId(session, std::stoull(row["objectId"].get<std::string>()));
        request.kind     = row.value("kind", "");
        request.extruder = row.value("extruder", 0);
        if (row.contains("settings"))
            request.settings = row["settings"].get<std::map<std::string, std::string>>();
        if (row.contains("geometry")) {
            const json&               g = row["geometry"];
            Workspace::RegionGeometry geometry;
            geometry.type   = g["type"];
            geometry.handle = g.value("handle", "");
            if (g.contains("center")) geometry.center = vec(g["center"]);
            if (g.contains("sizeMm")) geometry.size = vec(g["sizeMm"]);
            if (g.contains("axis")) geometry.normal = vec(g["axis"]);
            if (g.contains("vector")) geometry.normal = vec(g["vector"]);
            geometry.diameter          = g.value("diameterMm", 0.0);
            geometry.length            = g.value("lengthMm", 0.0);
            geometry.tolerance_degrees = g.value("toleranceDegrees", 0.0);
            request.geometry           = geometry;
        }
        requests.push_back(std::move(request));
    }
    return requests;
}

Workspace::PrinterSetupRequest setup_request(const json& arguments)
{
    Workspace::PrinterSetupRequest request;
    if (arguments.contains("printerPreset")) request.printer_preset = arguments["printerPreset"].get<std::string>();
    if (arguments.contains("plateType")) request.plate_type = arguments["plateType"].get<std::string>();
    if (arguments.contains("processPreset")) request.process_preset = arguments["processPreset"].get<std::string>();
    if (arguments.contains("filamentPresets")) request.filament_presets = arguments["filamentPresets"].get<std::vector<std::string>>();
    request.discard_unsaved_edits = arguments.value("unsavedEdits", "") == "discard";
    return request;
}

// What a setup would do, as the card and the staleness check compare it.
json setup_outcome(const Workspace::PrinterSetupPreview& preview)
{
    json filaments = json::array();
    for (const auto& filament : preview.resulting.filaments) filaments.push_back(filament.preset);
    return {{"printer", preview.resulting.preset}, {"plate", preview.resulting.plate_type},
            {"process", preview.process_preset}, {"filaments", std::move(filaments)},
            {"substituted", setup_substitutions_result(preview.substitutions)},
            {"discarded", setup_edits_result(preview.unsaved_edits)}};
}

// A settings call's target looked up in the open project. `found` is false
// when the call names an object the project does not have.
struct SettingsTargetLookup
{
    Workspace::SettingsTarget target;
    std::string               object_name;
    bool                      found{true};
};

SettingsTargetLookup settings_target(const json& arguments, const Workspace::WorkspaceSnapshot& snapshot)
{
    SettingsTargetLookup result;
    if (!arguments.contains("target"))
        return result;
    const std::string id = arguments["target"]["objectId"];
    for (const auto& plate : snapshot.plates)
        for (const auto& object : plate.objects)
            if (std::to_string(object.id.value()) == id) {
                result.target.object = object.id;
                result.object_name   = object.name;
            }
    result.found = result.target.object.has_value();
    return result;
}

constexpr const char* kMissingTarget = "That object is not in the open project. Read workspace_inspect again.";

Workspace::SettingsPatch settings_patch(const json& arguments, const Workspace::SettingsTarget& target)
{
    return {arguments.at("changes").get<std::map<std::string, std::string>>(), target};
}

json stale_settings_details(const json& arguments, const Workspace::WorkspaceSnapshot& snapshot)
{
    return {{"expectedSessionId", arguments.at("expectedSessionId")}, {"expectedRevision", arguments.at("expectedRevision")},
            {"currentSessionId", std::to_string(snapshot.session.value())}, {"currentRevision", snapshot.revision}};
}

} // namespace

ToolExecutionCoordinator::ToolExecutionCoordinator(Workspace::IWorkspace& workspace, const ToolRegistry& registry)
    : m_workspace(workspace), m_registry(registry), m_observers(std::make_shared<ObserverState>())
{
    m_workspace_subscription = m_workspace.subscribe([this](const Workspace::WorkspaceChanged& change) {
        if (!m_slice_handle.empty() && !m_slice_ended) {
            const bool running = m_workspace.snapshot().slicing.running;
            m_slice_seen_running = m_slice_seen_running || running;
            m_slice_ended        = m_slice_seen_running && !running;
        }
        invalidate_pending(change);
    });
}

ToolExecutionCoordinator::~ToolExecutionCoordinator()
{
    m_observers->alive = false;
    m_observers->observers.clear();
}

ToolActivitySubscription ToolExecutionCoordinator::subscribe(ActivityCallback listener)
{
    const std::uint64_t id = m_observers->next_id++;
    m_observers->observers.emplace(id, std::move(listener));
    std::weak_ptr<ObserverState> weak_state = m_observers;
    return ToolActivitySubscription([weak_state, id]() {
        if (auto state = weak_state.lock())
            state->observers.erase(id);
    });
}

const ToolActivity& ToolExecutionCoordinator::propose(const ToolRequest& request, const std::string& correlation_id,
                                                      ToolExecutionPacing pacing, ToolSource source)
{
    const Workspace::WorkspaceSnapshot snapshot = m_workspace.snapshot();
    const ToolDefinition* definition = m_registry.find(request.tool);

    ToolActivity activity;
    activity.action_id         = m_action_id_allocator ? m_action_id_allocator() : "t-" + std::to_string(m_next_action_id++);
    activity.correlation_id    = correlation_id;
    activity.source            = source;
    activity.server            = kServerName;
    activity.tool              = request.tool;
    activity.arguments_json    = request.arguments_json;
    activity.session           = snapshot.session.value();
    activity.expected_revision = snapshot.revision;
    activity.progress_total    = pacing.ticks > 0 ? pacing.ticks : 1;

    if (definition != nullptr) {
        activity.title             = definition->title;
        activity.action_class      = definition->action_class;
        activity.requires_approval = m_registry.requires_approval(*definition, request.arguments_json);
    } else {
        activity.title = "Unknown tool request";
    }

    m_activities.emplace_back(std::move(activity));
    ToolActivity& stored = m_activities.back();

    if (definition == nullptr) {
        fail(stored, "unknown_tool", "This build has no tool named \"" + request.tool + "\".");
        return stored;
    }

    ToolValidationResult validation = m_registry.validate_call(*definition, request.arguments_json);
    if (!validation.valid()) {
        fail(stored, validation.error->code, validation.error->message);
        return stored;
    }
    stored.arguments_json = std::move(validation.arguments_json);
    {
        const json arguments = json::parse(stored.arguments_json);
        stored.plan_id       = arguments.value("planId", "");
        // A plan waits for its card as a whole, including the members that
        // would otherwise run without one.
        if (!stored.plan_id.empty())
            stored.requires_approval = true;
        // A plan takes members only while its card is undecided and whole.
        const bool closed = std::any_of(m_activities.begin(), m_activities.end(), [&stored](const ToolActivity& member) {
            return member.action_id != stored.action_id && member.plan_id == stored.plan_id && member.state != ToolState::Pending;
        });
        if (!stored.plan_id.empty() && closed) {
            fail(stored, "plan_closed",
                 "Plan " + stored.plan_id + " was already decided, or one of its calls failed. Propose this with a new planId.");
            return stored;
        }
    }
    stored.title = m_registry.approval_title(*definition, stored.arguments_json);

    if (definition->handler == ToolHandler::HistoryRestore) {
        const auto arguments = json::parse(stored.arguments_json);
        if (arguments["sessionId"] != std::to_string(snapshot.session.value())) {
            fail(stored, "stale_id", "That history belongs to a project that is no longer open. Read it again.");
            return stored;
        }
        const auto history = m_workspace.history();
        const auto step    = std::find_if(history.steps.begin(), history.steps.end(), [&arguments](const Workspace::HistoryStep& candidate) {
            return std::to_string(candidate.id) == arguments["stepId"];
        });
        if (step == history.steps.end()) {
            fail(stored, "stale_id", "That step is no longer in the history. Read it again.");
            return stored;
        }
        const std::string name = step->label.empty() ? "an unnamed step" : "\xe2\x80\x9c" + step->label + "\xe2\x80\x9d";
        stored.title = (arguments["point"] == "before" ? "Go back to before " : "Go to just after ") + name;
        if (stored.title.size() > kToolLabelLimit) stored.title.resize(kToolLabelLimit - 3), stored.title += "...";
    }

    if (definition->handler == ToolHandler::ObjectImportFile) {
        const auto arguments = json::parse(stored.arguments_json);
        // Only the name is looked at before approval; the file is read after it.
        const std::filesystem::path path = std::filesystem::u8path(arguments["path"].get<std::string>());
        std::error_code error;
        if (!path.is_absolute() || !std::filesystem::is_regular_file(path, error)) {
            fail(stored, "invalid_argument", "Give the absolute path of a file that exists.");
            return stored;
        }
        stored.title = "Import " + arguments["path"].get<std::string>();
        if (stored.title.size() > kToolLabelLimit) stored.title.resize(kToolLabelLimit - 3), stored.title += "...";
    }

    const auto name_of_object = [&snapshot](const json& id) {
        for (const auto& plate : snapshot.plates)
            for (const auto& object : plate.objects)
                if (std::to_string(object.id.value()) == id) return object.name;
        return "object " + id.get<std::string>();
    };
    const auto bounded_title = [&stored]() {
        if (stored.title.size() > kToolLabelLimit) stored.title.resize(kToolLabelLimit - 3), stored.title += "...";
    };

    if (definition->handler == ToolHandler::ObjectDivide) {
        // The card says what the pieces will be, from the same dry run the
        // preview tool reports.
        const auto arguments = json::parse(stored.arguments_json);
        const auto object    = parse_object_argument(stored.arguments_json);
        const auto request   = divide_request(arguments);
        Workspace::DivideResult preview;
        const auto previewed = object ? m_workspace.preview_divide(*object, request, preview) :
                                        Workspace::CommandResult::failure(Workspace::WorkspaceError::InvalidId, "Object ID is invalid");
        if (!previewed.succeeded()) {
            fail(stored, workspace_error_code(previewed.error), previewed.message);
            return stored;
        }
        const std::string name = name_of_object(arguments["objectId"]);
        if (request.mode == Workspace::DivideRequest::Mode::Shells) {
            stored.title = "Split " + name + " into its " + std::to_string(preview.pieces.size()) + " shells as " +
                           (request.as_parts ? "parts of one object" : "separate objects");
        } else {
            const std::string kept = request.keep_upper && request.keep_lower ? "keeping both pieces" :
                                     request.keep_upper ? "keeping the upper piece" : "keeping the lower piece";
            stored.title = "Cut " + name + " by the plane through (" + number_words(request.point[0]) + ", " +
                           number_words(request.point[1]) + ", " + number_words(request.point[2]) + ") facing (" +
                           number_words(request.normal[0]) + ", " + number_words(request.normal[1]) + ", " +
                           number_words(request.normal[2]) + "), " + kept + (request.as_parts ? " as parts" : "");
        }
        stored.title += ":";
        for (std::size_t index = 0; index < preview.pieces.size(); ++index) {
            const auto& size = preview.pieces[index].size;
            stored.title += (index == 0 ? " " : ", ") + number_words(size[0]) + " x " + number_words(size[1]) + " x " +
                            number_words(size[2]) + " mm";
        }
        bounded_title();
    }

    if (definition->handler == ToolHandler::ExportFile) {
        // The path is checked, and the card says it, before anything is
        // written; the license is read from the project now.
        const auto arguments = json::parse(stored.arguments_json);
        const auto request   = export_request(arguments);
        const auto checked   = m_workspace.check_export(request);
        if (!checked.succeeded()) {
            fail(stored, workspace_error_code(checked.error), checked.message);
            return stored;
        }
        const std::string what = request.kind == "gcode"      ? "the G-code" :
                                 request.kind == "sliced_3mf" ? "the sliced plate" :
                                 request.kind == "project_3mf" ? "the project" :
                                 request.kind == "stl"        ? "an STL" : "the presets in use";
        std::error_code error;
        const bool replaces = std::filesystem::exists(std::filesystem::u8path(request.path), error) && request.kind != "presets";
        stored.title = "Export " + what + " to " + request.path + (replaces ? ", replacing it" : "");
        const std::string license = m_workspace.project_details().license;
        if (license_restricts(license))
            stored.title += "; the project's license is " + license;
        const auto warnings = export_slice_warnings(m_workspace, request);
        if (!warnings.empty())
            stored.title += "; the slice has " + std::to_string(warnings.size()) + (warnings.size() == 1 ? " warning: " : " warnings, first: ") +
                            warnings.front();
        bounded_title();
    }

    if (definition->handler == ToolHandler::ObjectMerge) {
        const auto arguments = json::parse(stored.arguments_json);
        stored.title = "Merge";
        const auto& ids = arguments["objectIds"];
        for (std::size_t index = 0; index < ids.size(); ++index)
            stored.title += (index == 0 ? " " : index + 1 == ids.size() ? " and " : ", ") + name_of_object(ids[index]);
        stored.title += " into one object";
        bounded_title();
    }

    if (definition->handler == ToolHandler::ObjectRepair) {
        const auto arguments = json::parse(stored.arguments_json);
        stored.title = "Repair the mesh of " + name_of_object(arguments["objectId"]);
        bounded_title();
    }

    if (definition->handler == ToolHandler::RegionAnnotate) {
        if (m_product_state == nullptr) {
            fail(stored, "unavailable_operation", "Region annotations are not available here.");
            return stored;
        }
        std::vector<Workspace::RegionRecord> planned;
        const auto plan = m_workspace.plan_regions(region_requests(json::parse(stored.arguments_json)), m_product_state->regions(), planned);
        if (!plan.succeeded()) {
            fail(stored, workspace_error_code(plan.error), plan.message);
            return stored;
        }
        stored.title = "Annotate";
        for (std::size_t index = 0; index < planned.size(); ++index)
            stored.title += (index == 0 ? " " : "; ") + planned[index].id + " " + planned[index].label;
        if (stored.title.size() > kToolLabelLimit) stored.title.resize(kToolLabelLimit - 3), stored.title += "...";
    }

    if (definition->handler == ToolHandler::ProjectDeleteItems) {
        const auto arguments = json::parse(stored.arguments_json);
        const auto object_name = [&snapshot](const std::string& id) {
            for (const auto& plate : snapshot.plates)
                for (const auto& object : plate.objects)
                    if (std::to_string(object.id.value()) == id) return object.name;
            return "object " + id;
        };
        stored.title = "Delete";
        bool first = true;
        for (const auto& row : arguments["items"]) {
            std::string item;
            if (row.contains("plateId")) {
                item = "plate " + row["plateId"].get<std::string>();
                for (const auto& plate : snapshot.plates)
                    if (std::to_string(plate.id.value()) == row["plateId"]) item = plate.name;
            } else if (row.contains("regionId")) {
                item = "region " + row["regionId"].get<std::string>();
                if (m_product_state != nullptr)
                    for (const auto& region : m_product_state->regions())
                        if (region.id == row["regionId"]) item += " (" + region.label + ")";
            } else if (row.contains("partId")) {
                item = "a part of " + object_name(row["objectId"]);
            } else if (row.contains("instance")) {
                item = "copy " + std::to_string(row["instance"].get<std::size_t>() + 1) + " of " + object_name(row["objectId"]);
            } else {
                item = object_name(row["objectId"]);
            }
            stored.title += (first ? " " : ", ") + item;
            first = false;
        }
        if (stored.title.size() > kToolLabelLimit) stored.title.resize(kToolLabelLimit - 3), stored.title += "...";
    }

    if (definition->handler == ToolHandler::PlateLayout) {
        const auto arguments = json::parse(stored.arguments_json);
        const auto name_of = [&snapshot](const std::string& id) {
            for (const auto& plate : snapshot.plates)
                for (const auto& object : plate.objects)
                    if (std::to_string(object.id.value()) == id) return object.name;
            return "object " + id;
        };
        const auto plate_name = [&snapshot](const std::string& id) {
            for (const auto& plate : snapshot.plates)
                if (std::to_string(plate.id.value()) == id) return plate.name;
            return "plate " + id;
        };
        std::vector<std::string> parts;
        for (const auto& row : arguments.value("objects", json::array())) {
            const std::string name = name_of(row["objectId"]);
            if (row.contains("quantity")) parts.push_back(name + " x" + std::to_string(row["quantity"].get<int>()));
            if (row.contains("enabled")) parts.push_back((row["enabled"].get<bool>() ? "print " : "skip ") + name);
            if (row.contains("plateId")) parts.push_back("move " + name + " to " + plate_name(row["plateId"]));
            if (row.contains("name")) parts.push_back("rename " + name + " to " + row["name"].get<std::string>());
            if (row.contains("extruder")) parts.push_back(name + " on filament " + std::to_string(row["extruder"].get<int>()));
        }
        for (const auto& row : arguments.value("plates", json::array())) {
            const std::string name = row.contains("plateId") ? plate_name(row["plateId"]) : "a new plate";
            if (!row.contains("plateId")) parts.push_back("add a plate");
            if (row.contains("name")) parts.push_back("name " + name + " " + row["name"].get<std::string>());
            if (row.contains("bedType")) parts.push_back(name + " on " + row["bedType"].get<std::string>());
        }
        if (arguments.contains("arrange"))
            parts.push_back(arguments["arrange"].contains("plateId") ? "arrange " + plate_name(arguments["arrange"]["plateId"])
                                                                      : "arrange all plates");
        stored.title = "Lay out:";
        for (std::size_t index = 0; index < parts.size(); ++index)
            stored.title += (index == 0 ? " " : ", ") + parts[index];
        if (stored.title.size() > kToolLabelLimit) stored.title.resize(kToolLabelLimit - 3), stored.title += "...";
    }

    if (definition->handler == ToolHandler::ObjectPlace) {
        const auto arguments = json::parse(stored.arguments_json);
        std::string name = "object " + arguments["objectId"].get<std::string>();
        for (const auto& plate : snapshot.plates)
            for (const auto& candidate : plate.objects)
                if (std::to_string(candidate.id.value()) == arguments["objectId"]) name = candidate.name;
        std::vector<std::string> parts;
        const auto number = [](double value) {
            std::ostringstream out;
            out << value;
            return out.str();
        };
        if (arguments.contains("unitsFix")) parts.push_back("convert from " + arguments["unitsFix"].get<std::string>());
        if (arguments.contains("scale"))
            parts.push_back("scale x" + number(arguments["scale"][0]) + " y" + number(arguments["scale"][1]) + " z" + number(arguments["scale"][2]));
        if (arguments.contains("scaleTo"))
            parts.push_back("scale to " + number(arguments["scaleTo"]["sizeMm"]) + " mm in " + arguments["scaleTo"]["axis"].get<std::string>());
        if (arguments.contains("mirrorAxis")) parts.push_back("mirror in " + arguments["mirrorAxis"].get<std::string>());
        if (arguments.contains("faceDown")) parts.push_back("put a chosen face down");
        if (arguments.contains("rotateDegrees"))
            parts.push_back("rotate " + number(arguments["rotateDegrees"][0]) + "/" + number(arguments["rotateDegrees"][1]) + "/" +
                            number(arguments["rotateDegrees"][2]) + " degrees");
        if (arguments.contains("autoOrient")) parts.push_back("auto-orient");
        if (arguments.contains("position"))
            parts.push_back("move to " + number(arguments["position"][0]) + ", " + number(arguments["position"][1]));
        if (arguments.contains("dropToBed")) parts.push_back("drop to the bed");
        stored.title = "Place " + name + ":";
        for (std::size_t index = 0; index < parts.size(); ++index)
            stored.title += (index == 0 ? " " : ", ") + parts[index];
        if (stored.title.size() > kToolLabelLimit) stored.title.resize(kToolLabelLimit - 3), stored.title += "...";
    }

    if (definition->handler == ToolHandler::PrinterSetup) {
        auto arguments = json::parse(stored.arguments_json);
        const Workspace::PrinterSetupRequest request = setup_request(arguments);
        if (!request.empty()) {
            const auto preview = m_workspace.preview_printer_setup(request);
            if (!preview.valid) {
                fail(stored, preview.issues.front().code, preview.issues.front().message,
                     printer_setup_preview_result(preview, json::array(), snapshot).dump());
                return stored;
            }
            if (!preview.unsaved_edits.empty() && !request.discard_unsaved_edits) {
                fail(stored, "unsaved_edits",
                     "This would drop unsaved preset edits. Ask the user, then call again with unsavedEdits \"discard\".",
                     printer_setup_preview_result(preview, json::array(), snapshot).dump());
                return stored;
            }
            // The approved outcome, compared again before anything is applied.
            arguments["confirmedSetup"] = setup_outcome(preview);
            stored.title = "Set up " + preview.resulting.preset;
            if (request.plate_type) stored.title += ", " + preview.resulting.plate_type;
            if (request.process_preset) stored.title += ", " + preview.process_preset;
            if (request.filament_presets)
                for (std::size_t slot = 0; slot < request.filament_presets->size(); ++slot)
                    stored.title += ", " + (*request.filament_presets)[slot];
            for (const auto& substitution : preview.substitutions)
                stored.title += "; replaces " + substitution.kind + " " + substitution.from;
            for (const auto& edits : preview.unsaved_edits)
                stored.title += "; discards " + std::to_string(edits.count) + " unsaved edits in " + edits.preset;
        } else {
            stored.title = "Record printer facts";
        }
        if (arguments.contains("confirmFacts")) {
            if (m_product_state == nullptr || !m_product_state->has_printer_facts()) {
                fail(stored, "unavailable_operation", "This build cannot record printer facts.");
                return stored;
            }
            for (const auto& fact : arguments["confirmFacts"])
                stored.title += "; " + fact["fact"].get<std::string>() + ": " + fact["value"].get<std::string>();
        }
        stored.arguments_json = arguments.dump();
        if (stored.title.size() > kToolLabelLimit) stored.title.resize(kToolLabelLimit - 3), stored.title += "...";
    }

    if (definition->handler == ToolHandler::ProjectOpen) {
        const auto arguments = json::parse(stored.arguments_json);
        const bool unsaved   = snapshot.setup.project_dirty || snapshot.setup.presets_dirty;
        if (unsaved && arguments.value("unsavedWork", "") != "discard") {
            fail(stored, "unsaved_work",
                 "The open project has unsaved changes. Ask the user; save it first, or call again with unsavedWork \"discard\".");
            return stored;
        }
        if (arguments.contains("path")) {
            // Only the name is looked at before approval; the file is read
            // after it.
            const std::filesystem::path path = std::filesystem::u8path(arguments["path"].get<std::string>());
            std::error_code error;
            if (!path.is_absolute() || !std::filesystem::is_regular_file(path, error)) {
                fail(stored, "invalid_argument", "Give the absolute path of a file that exists.");
                return stored;
            }
            if (path.extension() == ".3mf" && arguments.value("unitConversion", "keep") == "inches") {
                fail(stored, "invalid_argument", "A project keeps its own units; inches applies to model files.");
                return stored;
            }
            stored.title = "Open " + arguments["path"].get<std::string>();
        } else {
            stored.title = "Start a new project";
        }
        if (unsaved)
            stored.title += ", discarding unsaved changes" +
                            (snapshot.setup.project_name.empty() ? std::string() : " to " + snapshot.setup.project_name);
        if (stored.title.size() > kToolLabelLimit) stored.title.resize(kToolLabelLimit - 3), stored.title += "...";
    }

    if (definition->handler == ToolHandler::ProjectSave) {
        auto arguments = json::parse(stored.arguments_json);
        const std::string path = arguments.value("path", snapshot.setup.project_path);
        if (path.empty()) {
            fail(stored, "unavailable_operation", "This project has never been saved. Give the path to save it to.");
            return stored;
        }
        // Said before the card, not after approval: the adapter writes only a
        // project file.
        std::string extension = std::filesystem::u8path(path).extension().u8string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (extension != ".3mf") {
            fail(stored, "invalid_argument", "project_save writes a .3mf project. Write G-code, STL or presets with export_file.");
            return stored;
        }
        // The path is bound here, before approval, so the card names exactly
        // the file that will be written and a rename in between cannot move it.
        arguments["resolvedPath"] = path;
        stored.arguments_json     = arguments.dump();
        std::error_code error;
        const bool      replaces = std::filesystem::exists(std::filesystem::u8path(path), error);
        stored.title = std::string(replaces ? "Save the project, replacing " : "Save the project to ") + path;
        if (stored.title.size() > kToolLabelLimit) stored.title.resize(kToolLabelLimit - 3), stored.title += "...";
    }

    if (definition->handler == ToolHandler::SettingsApplyPatch) {
        auto arguments = json::parse(stored.arguments_json);
        if (!settings_revision_holds(arguments, snapshot, m_last_settings_revision)) {
            fail(stored, "stale_workspace", "The workspace changed. Read and preview again.", stale_settings_details(arguments, snapshot).dump());
            return stored;
        }
        const auto target = settings_target(arguments, snapshot);
        if (!target.found) {
            fail(stored, "missing_object", kMissingTarget);
            return stored;
        }
        const auto preview = m_workspace.preview_settings(settings_patch(arguments, target.target));
        if (!preview.valid) {
            const auto& issue = preview.issues.at(0);
            fail(stored, issue.code, issue.message, settings_preview_result(preview, snapshot).dump());
            return stored;
        }
        if (target.target.object) {
            stored.title = "Change " + std::to_string(preview.changes.size()) + " settings of " + target.object_name + ":";
            for (std::size_t index = 0; index < preview.changes.size(); ++index)
                stored.title += (index == 0 ? " " : ", ") + preview.changes[index].key;
            if (stored.title.size() > kToolLabelLimit) stored.title.resize(kToolLabelLimit - 3), stored.title += "...";
        }
        arguments["confirmedChanges"] = json::array();
        for (const auto& change : Workspace::settings_confirmation(preview))
            arguments["confirmedChanges"].push_back({{"key", change.key}, {"before", change.before}, {"after", change.after}});
        stored.arguments_json = arguments.dump();
    }

    notify(stored);
    if (!stored.requires_approval)
        start_running(stored);
    return stored;
}

bool ToolExecutionCoordinator::approve(const std::string& action_id)
{
    ToolActivity* activity = find_mutable(action_id);
    if (activity == nullptr || activity->state != ToolState::Pending)
        return false;

    // The eager invalidation below normally fails a stale proposal before a
    // decision can arrive; the session check remains as a belt for decisions
    // raced against a project replacement.
    if (m_workspace.snapshot().session.value() != activity->session) {
        fail(*activity, "stale_revision", "The project changed after this action was proposed. Ask the Agent again.");
        return false;
    }
    if (activity->plan_id.empty()) {
        start_running(*activity);
        return true;
    }
    // One decision for the whole plan, in the order it was proposed.
    const std::string plan = activity->plan_id;
    std::vector<std::string> members;
    for (const ToolActivity& member : m_activities)
        if (member.plan_id == plan && member.state == ToolState::Pending)
            members.push_back(member.action_id);
    for (const std::string& id : members)
        if (ToolActivity* member = find_mutable(id); member != nullptr && member->state == ToolState::Pending)
            start_running(*member);
    return true;
}

bool ToolExecutionCoordinator::reject(const std::string& action_id)
{
    ToolActivity* activity = find_mutable(action_id);
    if (activity == nullptr || activity->state != ToolState::Pending)
        return false;
    activity->state = ToolState::Rejected;
    notify(*activity);
    if (!activity->plan_id.empty()) {
        const std::string plan = activity->plan_id;
        std::vector<std::string> members;
        for (const ToolActivity& member : m_activities)
            if (member.plan_id == plan && member.state == ToolState::Pending)
                members.push_back(member.action_id);
        for (const std::string& id : members)
            if (ToolActivity* member = find_mutable(id); member != nullptr && member->state == ToolState::Pending) {
                member->state = ToolState::Rejected;
                notify(*member);
            }
    }
    return true;
}

bool ToolExecutionCoordinator::cancel(const std::string& action_id)
{
    ToolActivity* activity = find_mutable(action_id);
    if (activity == nullptr)
        return false;
    // Cancellation is possible while nothing durable has happened: before
    // approval, and while running before the final execution tick. Terminal
    // records never change.
    if (activity->state != ToolState::Pending && activity->state != ToolState::Approved &&
        activity->state != ToolState::Running)
        return false;
    activity->state = ToolState::Cancelled;
    notify(*activity);
    return true;
}

void ToolExecutionCoordinator::clear()
{
    m_activities.erase(std::remove_if(m_activities.begin(), m_activities.end(),
                                      [this](const ToolActivity& activity) { return activity.action_id != m_executing; }),
                       m_activities.end());
}

void ToolExecutionCoordinator::forget_if_closed(const std::string& action_id)
{
    // The record belongs to the project that was closed. Its subscribers have
    // had the terminal result; the new project's action ids start again, and
    // must not meet it.
    const std::uint64_t session = m_workspace.snapshot().session.value();
    m_activities.erase(std::remove_if(m_activities.begin(), m_activities.end(),
                                      [&](const ToolActivity& activity) {
                                          return activity.action_id == action_id && activity.session != session;
                                      }),
                       m_activities.end());
}

void ToolExecutionCoordinator::finish_slice_waits()
{
    for (auto wait = m_slice_waits.begin(); wait != m_slice_waits.end();) {
        ToolActivity* activity = find_mutable(wait->first);
        if (activity == nullptr || activity->state != ToolState::Running) {
            wait = m_slice_waits.erase(wait);
            continue;
        }
        const auto snapshot = m_workspace.snapshot();
        if (snapshot.session.value() != activity->session) {
            fail(*activity, "stale_revision", "The project changed while it was slicing.");
            wait = m_slice_waits.erase(wait);
            continue;
        }
        wait->second.seen_running = wait->second.seen_running || snapshot.slicing.running;
        bool sliced = !snapshot.plates.empty();
        for (const auto& plate : snapshot.plates)
            if ((!wait->second.plate || plate.id == *wait->second.plate) && !plate.sliced)
                sliced = false;
        const auto elapsed = std::chrono::steady_clock::now() - wait->second.started;
        // The run ends when it has been seen and stops. A run too quick to be
        // seen, or one Orca did not need, ends when the plates read as sliced;
        // a run that never reports ends the call after fifteen minutes.
        const bool ended = !snapshot.slicing.running &&
                           (wait->second.seen_running || (sliced && elapsed > std::chrono::seconds(3)) ||
                            elapsed > std::chrono::minutes(15));
        if (!ended) {
            ++wait;
            continue;
        }
        activity->result_json = json{{"handle", activity->action_id}, {"started", true}, {"finished", sliced},
                                     {"slicing", slicing_section_result(snapshot, m_slice_handle)},
                                     {"sessionId", std::to_string(snapshot.session.value())},
                                     {"revision", snapshot.revision}}
                                    .dump();
        activity->state = ToolState::Succeeded;
        wait = m_slice_waits.erase(wait);
        notify(*activity);
    }
}

void ToolExecutionCoordinator::pump()
{
    finish_slice_waits();
    std::set<std::string> busy_plans; // plans with an earlier member still running
    for (ToolActivity& activity : m_activities) {
        if (activity.state != ToolState::Running || m_slice_waits.count(activity.action_id)) {
            if (!activity.plan_id.empty() && activity.state == ToolState::Running)
                busy_plans.insert(activity.plan_id);
            continue;
        }
        if (!activity.plan_id.empty() && busy_plans.count(activity.plan_id))
            continue;
        if (activity.progress_current + 1 < activity.progress_total) {
            ++activity.progress_current;
            notify(activity);
        } else {
            activity.progress_current = activity.progress_total;
            execute(activity);
        }
        // One activity per tick keeps event ordering deterministic.
        return;
    }
}

bool ToolExecutionCoordinator::any_running() const
{
    for (const ToolActivity& activity : m_activities)
        if (activity.state == ToolState::Running)
            return true;
    return false;
}

void ToolExecutionCoordinator::forget_terminal_activities(const std::vector<std::string>& message_ids)
{
    const auto belongs_to_chat = [&](const ToolActivity& activity) {
        return std::find(message_ids.begin(), message_ids.end(), activity.correlation_id) != message_ids.end();
    };
    for (const auto& activity : m_activities)
        if (belongs_to_chat(activity) && !tool_state_terminal(activity.state))
            throw std::logic_error("Cannot forget an in-flight tool activity");
    m_activities.erase(std::remove_if(m_activities.begin(), m_activities.end(), [&](const ToolActivity& activity) {
        return belongs_to_chat(activity);
    }), m_activities.end());
}

const ToolActivity* ToolExecutionCoordinator::find(const std::string& action_id) const
{
    for (const ToolActivity& activity : m_activities)
        if (activity.action_id == action_id)
            return &activity;
    return nullptr;
}

ToolActivity* ToolExecutionCoordinator::find_mutable(const std::string& action_id)
{
    for (ToolActivity& activity : m_activities)
        if (activity.action_id == action_id)
            return &activity;
    return nullptr;
}

void ToolExecutionCoordinator::start_running(ToolActivity& activity)
{
    activity.state = ToolState::Approved;
    notify(activity);
    activity.state = ToolState::Running;
    notify(activity);
}

void ToolExecutionCoordinator::execute(ToolActivity& activity)
{
    // Approval and the execution tick are separate GUI events. Recheck the
    // last content/history change here; selection-only changes still cannot
    // redirect a proposal pinned to an object ID and do not invalidate it.
    // Every mutation is rechecked, not only the ones that waited for a card: a
    // computation-only action skips approval, not staleness.
    // An approved plan's members follow one another: the changes its own
    // earlier members made do not make the later ones stale, and any other
    // change since the proposal does.
    const std::uint64_t invalidated = settings_patch_tool(activity.tool) ? m_last_settings_revision : m_last_invalidating_revision;
    const bool          changed     = activity.plan_id.empty() ? invalidated > activity.expected_revision :
                                                                 m_disturbed_plans.count(activity.plan_id) != 0;
    if (activity.action_class != ActionClass::ReadOnly &&
        (m_workspace.snapshot().session.value() != activity.session || changed)) {
        fail(activity, "stale_revision", "The project changed before this action could execute. Propose it again.");
        return;
    }
    // Whatever this action changes is the Agent's, in the change log.
    const Workspace::IWorkspace::AgentEdit agent_edit(m_workspace);
    m_executing = activity.action_id;
    struct Finished { std::string& id; ~Finished() { id.clear(); } } finished{m_executing};
    const ToolDefinition* definition = m_registry.find(activity.tool);
    if (definition == nullptr) {
        fail(activity, "unknown_tool", "This build has no tool named \"" + activity.tool + "\".");
        return;
    }

    if (definition->handler == ToolHandler::WorkspaceInspect) {
        const auto arguments = json::parse(activity.arguments_json);
        InspectSections sections;
        if (arguments.contains("sections")) {
            const auto asked = [&arguments](const char* name) {
                const auto& sections = arguments["sections"];
                return std::any_of(sections.begin(), sections.end(),
                                   [name](const json& value) { return value == name; });
            };
            sections = {asked("summary"), asked("intent"), asked("plan"), asked("slicing"), asked("history"), asked("printer"), asked("project"), asked("objects"), asked("activities")};
        }
        if ((sections.intent || sections.plan || sections.printer) && m_product_state == nullptr) {
            fail(activity, "unavailable_operation", "This build cannot read the intent or the plan.");
            return;
        }
        json result = workspace_inspection(m_workspace.snapshot(), sections);
        if (sections.intent) result["intent"] = intent_section_result(m_product_state->print_intent());
        if (sections.plan) result["plan"] = plan_section_result(m_product_state->plan());
        if (sections.slicing) result["slicing"] = slicing_section_result(m_workspace.snapshot(), m_slice_handle);
        if (sections.printer) {
            const auto configured = m_workspace.configured_printer();
            const auto devices    = m_workspace.printers();
            const auto facts      = m_product_state->has_printer_facts() ?
                                        m_product_state->printer_facts(printer_fact_key(configured, devices)) :
                                        std::vector<Workspace::PrinterFact>{};
            result["printer"] = printer_section_result(configured, devices, facts);
        }
        if (sections.objects) result["objects"] = objects_section_result(m_workspace.object_details());
        if (sections.project) result["project"] = project_section_result(m_workspace.snapshot(), m_workspace.project_details());
        if (sections.history) result["history"] = history_section_result(m_workspace.snapshot(), m_workspace.history());
        if (sections.activities) {
            // The latest calls of either adapter, oldest first: how a queued
            // plan's members ended, after the turn that proposed them.
            constexpr std::size_t kRecentActivities = 20;
            result["activities"] = json::array();
            const std::size_t first = m_activities.size() > kRecentActivities + 1 ? m_activities.size() - kRecentActivities - 1 : 0;
            for (std::size_t index = first; index < m_activities.size(); ++index) {
                const ToolActivity& recent = m_activities[index];
                if (recent.action_id == activity.action_id)
                    continue;
                json row{{"actionId", recent.action_id}, {"tool", recent.tool}, {"title", recent.title}, {"state", tool_state_name(recent.state)}};
                if (!recent.plan_id.empty()) row["planId"] = recent.plan_id;
                if (recent.error) row["error"] = json{{"code", recent.error->code}, {"message", recent.error->message}};
                result["activities"].push_back(std::move(row));
            }
        }
        activity.result_json = result.dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::HistoryRestore) {
        const auto arguments = json::parse(activity.arguments_json);
        const auto restored  = m_workspace.restore_history(std::stoull(arguments["stepId"].get<std::string>()),
                                                           arguments["point"] == "before" ? Workspace::HistoryPoint::Before :
                                                                                            Workspace::HistoryPoint::After);
        if (!restored.succeeded()) {
            fail(activity, workspace_error_code(restored.error), restored.message);
            return;
        }
        const auto snapshot = m_workspace.snapshot();
        // What Orca's history does not carry, so the agent can say it stayed.
        json kept = json::array();
        if (!snapshot.preset_deltas.empty())
            kept.push_back(std::to_string(snapshot.preset_deltas.size()) + " edited process settings");
        if (m_product_state != nullptr && !m_product_state->print_intent().empty())
            kept.push_back("the print intent");
        if (m_product_state != nullptr && !m_product_state->plan().headline.empty())
            kept.push_back("the plan");
        activity.result_json = json{{"history", history_section_result(snapshot, m_workspace.history())},
                                    {"notReversed", {{"items", std::move(kept)}, {"truncated", false}}},
                                    {"sessionId", std::to_string(snapshot.session.value())},
                                    {"revision", snapshot.revision}}
                                   .dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::ProjectOpen) {
        const auto arguments = json::parse(activity.arguments_json);
        Workspace::ProjectOpenRequest request;
        request.path                  = arguments.value("path", "");
        request.new_project           = arguments.value("new", false);
        request.load_project_settings = arguments.value("loadProjectSettings", "project") == "project";
        const std::string units       = arguments.value("unitConversion", "keep");
        request.units                 = units == "inches"        ? Workspace::UnitChoice::Inches :
                                        units == "convertIfTiny" ? Workspace::UnitChoice::ConvertIfTiny :
                                                                   Workspace::UnitChoice::Keep;
        request.scale_oversized       = arguments.value("oversized", "keep") == "scaleToFit";
        request.discard_unsaved       = arguments.value("unsavedWork", "") == "discard";
        // The conversation belongs to the project being closed; it goes to
        // its recovery state before the project does.
        if (m_product_state != nullptr)
            m_product_state->flush_to_project();
        std::vector<Workspace::LoadDecision> decisions;
        const std::string action_id = activity.action_id;
        const auto opened = m_workspace.open_project(request, decisions);
        // Replacing the project clears the other activities, which may move
        // this one: find it again rather than trust the reference.
        ToolActivity& current = *find_mutable(action_id);
        json asked = json::array();
        for (const auto& decision : decisions)
            if (asked.size() < 32) asked.push_back({{"question", decision.question}, {"answer", decision.answer}});
        if (!opened.succeeded()) {
            fail(current, workspace_error_code(opened.error), opened.message, json{{"decisions", asked}}.dump());
            forget_if_closed(action_id);
            return;
        }
        const auto snapshot = m_workspace.snapshot();
        std::size_t objects = 0;
        for (const auto& plate : snapshot.plates) objects += plate.objects.size();
        current.result_json = json{{"projectName", snapshot.setup.project_name}, {"path", snapshot.setup.project_path},
                                    {"plateCount", snapshot.plates.size()}, {"objectCount", objects},
                                    {"decisions", std::move(asked)},
                                    {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}}
                                   .dump();
        current.state = ToolState::Succeeded;
        notify(current);
        forget_if_closed(action_id);
        return;
    }

    if (definition->handler == ToolHandler::ProjectSave) {
        const std::string path = json::parse(activity.arguments_json).at("resolvedPath").get<std::string>();
        // The conversation and the rest of the product state travel inside the
        // project file, so what is still pending goes to disk first.
        if (m_product_state != nullptr)
            m_product_state->flush_to_project();
        const auto saved = m_workspace.save_project(path);
        if (!saved.succeeded()) {
            fail(activity, workspace_error_code(saved.error), saved.message);
            return;
        }
        const auto snapshot  = m_workspace.snapshot();
        activity.result_json = json{{"path", path}, {"saved", true}, {"projectDirty", snapshot.setup.project_dirty},
                                    {"sessionId", std::to_string(snapshot.session.value())},
                                    {"revision", snapshot.revision}}
                                   .dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::ObjectImport || definition->handler == ToolHandler::ObjectImportFile) {
        const auto arguments = json::parse(activity.arguments_json);
        const Workspace::ProjectSessionId session(std::stoull(arguments["sessionId"].get<std::string>()));
        Workspace::ImportRequest request;
        if (definition->handler == ToolHandler::ObjectImport) {
            request.path = m_attachment_path_resolver ? m_attachment_path_resolver(arguments["attachmentId"].get<std::string>()) : std::string();
            if (request.path.empty()) {
                fail(activity, "unavailable_operation", "The attached model is no longer available to import.");
                return;
            }
        } else {
            request.path = arguments["path"].get<std::string>();
        }
        if (arguments.contains("plateId"))
            request.plate = Workspace::PlateId(session, std::stoull(arguments["plateId"].get<std::string>()));
        const std::string units = arguments.value("unitConversion", "keep");
        request.units           = units == "inches"        ? Workspace::UnitChoice::Inches :
                                  units == "convertIfTiny" ? Workspace::UnitChoice::ConvertIfTiny :
                                                             Workspace::UnitChoice::Keep;
        request.scale_oversized = arguments.value("oversized", "keep") == "scaleToFit";
        if (session != m_workspace.snapshot().session) {
            fail(activity, "stale_id", "That session is no longer open.");
            return;
        }
        std::vector<Workspace::LoadDecision> decisions;
        std::vector<Workspace::ObjectId>     added;
        const auto imported = m_workspace.import_objects(request, decisions, added);
        json asked = json::array(), ids = json::array();
        for (const auto& decision : decisions)
            if (asked.size() < 32) asked.push_back({{"question", decision.question}, {"answer", decision.answer}});
        if (!imported.succeeded()) {
            fail(activity, workspace_error_code(imported.error), imported.message, json{{"decisions", asked}}.dump());
            return;
        }
        for (const auto& id : added)
            if (ids.size() < 64) ids.push_back(std::to_string(id.value()));
        // The new objects as they are now, sizes included, so a conversion
        // Orca already made is visible.
        std::vector<Workspace::ObjectDetails> rows;
        for (const auto& row : m_workspace.object_details())
            if (std::find(added.begin(), added.end(), row.id) != added.end()) rows.push_back(row);
        const auto snapshot  = m_workspace.snapshot();
        activity.result_json = json{{"objectIds", std::move(ids)}, {"objects", objects_section_result(rows)}, {"decisions", std::move(asked)},
                                    {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}}
                                   .dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::ProjectDeleteItems) {
        const auto arguments = json::parse(activity.arguments_json);
        const Workspace::ProjectSessionId session(std::stoull(arguments["sessionId"].get<std::string>()));
        std::vector<Workspace::DeleteItem> items;
        std::vector<Workspace::RegionRecord> regions, kept;
        std::set<std::string>                removed;
        for (const auto& row : arguments["items"])
            if (row.contains("regionId"))
                removed.insert(row["regionId"].get<std::string>());
        if (!removed.empty()) {
            if (m_product_state == nullptr) {
                fail(activity, "unavailable_operation", "Region annotations are not available here.");
                return;
            }
            for (const Workspace::RegionRecord& record : m_product_state->regions())
                (removed.count(record.id) ? regions : kept).push_back(record);
            if (regions.size() != removed.size()) {
                fail(activity, "invalid_argument", "A named region is not in the project; read object_analyze's regions section.");
                return;
            }
        }
        for (const auto& row : arguments["items"]) {
            Workspace::DeleteItem item;
            if (row.contains("regionId"))
                continue;
            if (row.contains("plateId")) {
                item.kind  = Workspace::DeleteItem::Kind::Plate;
                item.plate = Workspace::PlateId(session, std::stoull(row["plateId"].get<std::string>()));
            } else {
                item.object = Workspace::ObjectId(session, std::stoull(row["objectId"].get<std::string>()));
                if (row.contains("partId")) {
                    item.kind = Workspace::DeleteItem::Kind::Part;
                    item.part = std::stoull(row["partId"].get<std::string>());
                } else if (row.contains("instance")) {
                    item.kind     = Workspace::DeleteItem::Kind::Instance;
                    item.instance = row["instance"].get<std::size_t>();
                }
            }
            items.push_back(item);
        }
        const auto deleted = items.empty() ? Workspace::CommandResult::success() : m_workspace.delete_items(items);
        if (!deleted.succeeded()) {
            fail(activity, workspace_error_code(deleted.error), deleted.message);
            return;
        }
        if (!regions.empty()) {
            // The artifacts go as their own undo step; the records go with them.
            const auto cleared = m_workspace.remove_regions(regions);
            if (!cleared.succeeded()) {
                fail(activity, workspace_error_code(cleared.error), cleared.message);
                return;
            }
            m_product_state->set_regions(kept);
        }
        const auto snapshot = m_workspace.snapshot();
        json       result{{"objects", objects_section_result(m_workspace.object_details())},
                          {"plateCount", snapshot.plates.size()},
                          {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}};
        if (!removed.empty())
            result["removedRegions"] = json(std::vector<std::string>(removed.begin(), removed.end()));
        activity.result_json = result.dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::PlateLayout) {
        const auto arguments = json::parse(activity.arguments_json);
        const Workspace::ProjectSessionId session(std::stoull(arguments["sessionId"].get<std::string>()));
        Workspace::LayoutRequest request;
        for (const auto& row : arguments.value("objects", json::array())) {
            Workspace::LayoutObject object;
            object.id = Workspace::ObjectId(session, std::stoull(row["objectId"].get<std::string>()));
            if (row.contains("enabled")) object.enabled = row["enabled"].get<bool>();
            if (row.contains("quantity")) object.quantity = row["quantity"].get<std::size_t>();
            if (row.contains("plateId")) object.plate = Workspace::PlateId(session, std::stoull(row["plateId"].get<std::string>()));
            if (row.contains("name")) object.name = row["name"].get<std::string>();
            if (row.contains("extruder")) object.extruder = row["extruder"].get<int>();
            request.objects.push_back(std::move(object));
        }
        for (const auto& row : arguments.value("plates", json::array())) {
            Workspace::LayoutPlate plate;
            if (row.contains("plateId")) plate.id = Workspace::PlateId(session, std::stoull(row["plateId"].get<std::string>()));
            if (row.contains("name")) plate.name = row["name"].get<std::string>();
            if (row.contains("bedType")) plate.bed_type = row["bedType"].get<std::string>();
            request.plates.push_back(std::move(plate));
        }
        if (arguments.contains("arrange")) {
            Workspace::LayoutArrange arrange;
            const auto& row = arguments["arrange"];
            if (row.contains("plateId")) arrange.plate = Workspace::PlateId(session, std::stoull(row["plateId"].get<std::string>()));
            if (row.contains("spacingMm")) arrange.spacing = row["spacingMm"].get<double>();
            if (row.contains("allowRotation")) arrange.rotation = row["allowRotation"].get<bool>();
            request.arrange = arrange;
        }
        Workspace::LayoutResult laid;
        const auto done = m_workspace.lay_out(request, activity.action_id, laid);
        if (!done.succeeded()) {
            fail(activity, workspace_error_code(done.error), done.message);
            return;
        }
        const auto snapshot = m_workspace.snapshot();
        json added = json::array();
        for (const auto& plate : laid.added_plates) added.push_back(std::to_string(plate.value()));
        json result{{"objects", objects_section_result(m_workspace.object_details())}, {"addedPlateIds", std::move(added)},
                    {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}};
        if (laid.arranging) result["handle"] = activity.action_id;
        activity.result_json = result.dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::ObjectPlace) {
        const auto arguments = json::parse(activity.arguments_json);
        const auto object    = parse_object_argument(activity.arguments_json);
        Workspace::PlacementRequest request;
        request.instance  = arguments.value("instance", std::size_t(0));
        request.units_fix = arguments.value("unitsFix", "");
        if (arguments.contains("scale"))
            request.scale = Workspace::Vec3{arguments["scale"][0].get<double>(), arguments["scale"][1].get<double>(),
                                            arguments["scale"][2].get<double>()};
        if (arguments.contains("scaleTo"))
            request.scale_to = std::make_pair(arguments["scaleTo"]["axis"] == "x" ? 0 : arguments["scaleTo"]["axis"] == "y" ? 1 : 2,
                                              arguments["scaleTo"]["sizeMm"].get<double>());
        request.mirror_axis = arguments.value("mirrorAxis", "");
        request.face_down   = arguments.value("faceDown", "");
        if (arguments.contains("rotateDegrees"))
            request.rotate = Workspace::Vec3{arguments["rotateDegrees"][0].get<double>(), arguments["rotateDegrees"][1].get<double>(),
                                             arguments["rotateDegrees"][2].get<double>()};
        if (arguments.contains("position"))
            request.position = std::array<double, 2>{arguments["position"][0].get<double>(), arguments["position"][1].get<double>()};
        request.drop_to_bed = arguments.contains("dropToBed");
        request.auto_orient = arguments.contains("autoOrient");
        Workspace::PlacementResult placed;
        const auto bound_before = bound_regions();
        const auto done = object ? m_workspace.place_object(*object, request, activity.action_id, placed) :
                                   Workspace::CommandResult::failure(Workspace::WorkspaceError::InvalidId, "Object ID is invalid");
        if (!done.succeeded()) {
            fail(activity, workspace_error_code(done.error), done.message);
            return;
        }
        const auto snapshot = m_workspace.snapshot();
        // Tenths of a micron and a thousandth of a degree; `+ 0.0` drops -0.
        const auto rounded = [](const std::array<double, 3>& v, double factor) {
            return json::array({std::round(v[0] * factor) / factor + 0.0, std::round(v[1] * factor) / factor + 0.0,
                                std::round(v[2] * factor) / factor + 0.0});
        };
        const double degrees = 180.0 / 3.141592653589793;
        json result{{"objectId", std::to_string(placed.object.value())},
                    {"transform", {{"positionMm", rounded(placed.transform.position, 1e4)},
                                   {"rotationDegrees", rounded({placed.transform.rotation[0] * degrees, placed.transform.rotation[1] * degrees,
                                                                placed.transform.rotation[2] * degrees}, 1e3)},
                                   {"scale", rounded(placed.transform.scale, 1e6)}}},
                    {"sizeMm", rounded(placed.size, 1e4)},
                    {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}};
        if (placed.orienting)
            result["handle"] = activity.action_id;
        result["regionsUnbound"] = regions_unbound(bound_before);
        activity.result_json = result.dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::ViewRender) {
        const auto arguments = json::parse(activity.arguments_json);
        const auto snapshot  = m_workspace.snapshot();
        Workspace::RenderRequest request;
        if (arguments.contains("plateId"))
            request.plate = Workspace::PlateId(snapshot.session, std::stoull(arguments["plateId"].get<std::string>()));
        request.view   = arguments.value("view", "iso");
        request.width  = arguments.value("widthPx", 1024);
        request.height = arguments.value("heightPx", 768);
        Workspace::RenderedImage rendered;
        const auto done = m_workspace.render_view(request, rendered);
        if (!done.succeeded()) {
            fail(activity, workspace_error_code(done.error), done.message);
            return;
        }
        activity.image = std::make_shared<ToolImage>(ToolImage{"image/png", base64_bytes(rendered.png), rendered.width, rendered.height});
        activity.result_json = json{{"plateId", std::to_string(rendered.plate.value())}, {"view", rendered.view},
                                    {"widthPx", rendered.width}, {"heightPx", rendered.height}, {"mimeType", "image/png"},
                                    {"bytes", rendered.png.size()},
                                    {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}}
                                   .dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::AttachmentRead) {
        const auto arguments = json::parse(activity.arguments_json);
        Workspace::AttachmentContent content;
        const auto done = m_workspace.read_attachment(arguments["id"], content);
        if (!done.succeeded()) {
            fail(activity, workspace_error_code(done.error), done.message);
            return;
        }
        const auto snapshot = m_workspace.snapshot();
        json result{{"id", content.id}, {"folder", content.folder}, {"kind", content.kind}, {"mimeType", content.mime_type},
                    {"bytes", content.bytes}, {"truncated", content.truncated},
                    {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}};
        if (content.kind == "image") {
            activity.image = std::make_shared<ToolImage>(ToolImage{content.mime_type, base64_bytes(content.data), content.width, content.height});
            result["widthPx"]  = content.width;
            result["heightPx"] = content.height;
        } else if (content.kind == "text") {
            result["text"] = content.data;
        }
        activity.result_json = result.dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::SliceInspect) {
        const auto arguments = json::parse(activity.arguments_json);
        const auto snapshot  = m_workspace.snapshot();
        std::optional<Workspace::PlateId> plate = snapshot.active_plate;
        if (arguments.contains("plateId"))
            plate = Workspace::PlateId(snapshot.session, std::stoull(arguments["plateId"].get<std::string>()));
        if (!plate) {
            fail(activity, "unavailable_operation", "This project has no plate to inspect.");
            return;
        }
        Workspace::SliceInspectRequest request;
        request.plate = *plate;
        request.gcode = arguments["view"] == "gcode";
        request.first = arguments.value("first", std::size_t(0));
        request.count = arguments.value("count", std::size_t(100));
        const auto inspection = m_workspace.inspect_slice(request);
        json result{{"valid", inspection.valid}, {"plateId", std::to_string(plate->value())}, {"view", arguments["view"]},
                    {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}};
        const auto range = [](double low, double high) {
            return json{{"before", std::round(low * 100) / 100}, {"after", std::round(high * 100) / 100}};
        };
        if (inspection.valid && request.gcode) {
            result["gcode"]     = inspection.gcode;
            result["firstLine"] = inspection.first_line;
        } else if (inspection.valid) {
            json layers = json::array();
            for (const Workspace::SliceLayer& layer : inspection.layers)
                layers.push_back({{"index", layer.index}, {"zMm", std::round(layer.z * 1000) / 1000},
                                  {"heightMm", std::round(layer.height * 1000) / 1000}, {"seconds", std::round(layer.seconds * 10) / 10},
                                  {"roles", layer.roles}, {"speedMmS", range(layer.speed_min, layer.speed_max)},
                                  {"fanPercent", range(layer.fan_min, layer.fan_max)},
                                  {"temperatureC", range(layer.temperature_min, layer.temperature_max)},
                                  {"flowMm3S", range(layer.flow_min, layer.flow_max)}});
            result["layers"]     = std::move(layers);
            result["layerCount"] = inspection.layer_count;
        }
        if (inspection.next)
            result["next"] = *inspection.next;
        activity.result_json = result.dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::ActivityCancel) {
        const std::string handle = json::parse(activity.arguments_json)["handle"];
        const auto        before = m_workspace.snapshot();
        std::string kind = "none", message;
        bool        cancelled = false;
        const ToolActivity* target = find(handle);
        if (target != nullptr && target->action_id != activity.action_id && target->state == ToolState::Pending) {
            kind      = "proposal";
            cancelled = cancel(handle);
            message   = cancelled ? "The waiting call was cancelled." : "The call had already been decided.";
        } else if (handle == m_slice_handle && !handle.empty()) {
            kind = "slice";
            if (before.slicing.running && !m_slice_ended)
                m_workspace.cancel_slice(cancelled);
            message = cancelled ? "The slice was stopped." :
                                  "That slice has ended; a slice started since is not the tools' to stop.";
        } else if (std::any_of(before.jobs.begin(), before.jobs.end(), [&handle](const auto& job) { return job.handle == handle; })) {
            kind = "job";
            const auto done = m_workspace.cancel_job(handle, cancelled);
            if (!done.succeeded()) {
                fail(activity, workspace_error_code(done.error), done.message);
                return;
            }
            message = cancelled ? "Cancelling the job; its end shows in workspace_inspect's slicing section." : "That job had already ended.";
        } else {
            fail(activity, "invalid_argument", "Nothing the tools started has the handle " + handle + ".");
            return;
        }
        const auto snapshot  = m_workspace.snapshot();
        activity.result_json = json{{"handle", handle}, {"kind", kind}, {"cancelled", cancelled}, {"message", message},
                                    {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}}
                                   .dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::ExportFile) {
        const auto request = export_request(json::parse(activity.arguments_json));
        Workspace::ExportResult exported;
        const auto done = m_workspace.export_file(request, exported);
        if (!done.succeeded()) {
            fail(activity, workspace_error_code(done.error), done.message);
            return;
        }
        const std::string license  = m_workspace.project_details().license;
        const auto        snapshot = m_workspace.snapshot();
        json              files    = json::array();
        for (const std::string& file : exported.files)
            if (files.size() < 32)
                files.push_back(file);
        json result{{"kind", request.kind}, {"files", std::move(files)}, {"bytes", exported.bytes},
                    {"license", license}, {"licenseRestricted", license_restricts(license)},
                    {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}};
        if (request.kind == "gcode" || request.kind == "sliced_3mf") {
            result["sliceWarnings"] = json::array();
            for (const std::string& warning : export_slice_warnings(m_workspace, request))
                if (result["sliceWarnings"].size() < 8)
                    result["sliceWarnings"].push_back(warning.substr(0, 512));
        }
        activity.result_json = result.dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::ObjectDividePreview) {
        const auto object = parse_object_argument(activity.arguments_json);
        Workspace::DivideResult preview;
        const auto previewed = object ? m_workspace.preview_divide(*object, divide_request(json::parse(activity.arguments_json)), preview) :
                                        Workspace::CommandResult::failure(Workspace::WorkspaceError::InvalidId, "Object ID is invalid");
        if (!previewed.succeeded()) {
            fail(activity, workspace_error_code(previewed.error), previewed.message);
            return;
        }
        // Dividing replaces the object's mesh, so every region still bound to
        // it would lose its binding.
        json unbound = json::array();
        if (m_product_state != nullptr)
            for (const Workspace::RegionStatus& status : m_workspace.region_status(m_product_state->regions()))
                if (status.object && *status.object == *object && !status.binding_lost)
                    unbound.push_back(status.record.id);
        activity.result_json = divide_result(preview, std::move(unbound), m_workspace.snapshot()).dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::ObjectDivide) {
        const auto object       = parse_object_argument(activity.arguments_json);
        const auto bound_before = bound_regions();
        Workspace::DivideResult divided;
        const auto done = object ? m_workspace.divide_object(*object, divide_request(json::parse(activity.arguments_json)), divided) :
                                   Workspace::CommandResult::failure(Workspace::WorkspaceError::InvalidId, "Object ID is invalid");
        if (!done.succeeded()) {
            fail(activity, workspace_error_code(done.error), done.message);
            return;
        }
        activity.result_json = divide_result(divided, regions_unbound(bound_before), m_workspace.snapshot()).dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::ObjectMerge) {
        const auto arguments = json::parse(activity.arguments_json);
        const Workspace::ProjectSessionId session(std::stoull(arguments["sessionId"].get<std::string>()));
        std::vector<Workspace::ObjectId>  ids;
        for (const json& id : arguments["objectIds"])
            ids.emplace_back(session, std::stoull(id.get<std::string>()));
        const auto            bound_before = bound_regions();
        Workspace::ObjectId   merged;
        const auto            done = m_workspace.merge_objects(ids, merged);
        if (!done.succeeded()) {
            fail(activity, workspace_error_code(done.error), done.message);
            return;
        }
        const auto snapshot  = m_workspace.snapshot();
        activity.result_json = json{{"objectId", std::to_string(merged.value())}, {"regionsUnbound", regions_unbound(bound_before)},
                                    {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}}
                                   .dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::ObjectRepair) {
        const auto object       = parse_object_argument(activity.arguments_json);
        const auto bound_before = bound_regions();
        Workspace::RepairResult repaired;
        const auto done = object ? m_workspace.repair_object(*object, repaired) :
                                   Workspace::CommandResult::failure(Workspace::WorkspaceError::InvalidId, "Object ID is invalid");
        if (!done.succeeded()) {
            fail(activity, workspace_error_code(done.error), done.message);
            return;
        }
        const auto snapshot = m_workspace.snapshot();
        const auto pair     = [](double before, double after) {
            return json{{"before", std::round(before * 100) / 100}, {"after", std::round(after * 100) / 100}};
        };
        activity.result_json =
            json{{"changed", repaired.changed},
                 {"openEdges", pair(double(repaired.open_edges_before), double(repaired.open_edges_after))},
                 {"facets", pair(double(repaired.facets_before), double(repaired.facets_after))},
                 {"parts", pair(double(repaired.parts_before), double(repaired.parts_after))},
                 {"volumeMm3", pair(repaired.volume_before, repaired.volume_after)},
                 {"regionsUnbound", regions_unbound(bound_before)},
                 {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}}
                .dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::RegionAnnotate) {
        std::vector<Workspace::RegionRecord> stored = m_product_state->regions(), planned, applied, replaced;
        const auto plan = m_workspace.plan_regions(region_requests(json::parse(activity.arguments_json)), stored, planned);
        if (!plan.succeeded()) {
            fail(activity, workspace_error_code(plan.error), plan.message);
            return;
        }
        for (const Workspace::RegionRecord& record : stored)
            if (std::any_of(planned.begin(), planned.end(), [&record](const auto& p) { return p.id == record.id; }))
                replaced.push_back(record);
        const auto done = m_workspace.apply_regions(planned, replaced, applied);
        if (!done.succeeded()) {
            fail(activity, workspace_error_code(done.error), done.message);
            return;
        }
        // Replaced records keep their place; new ones go last.
        for (Workspace::RegionRecord& record : stored)
            for (const Workspace::RegionRecord& written : applied)
                if (written.id == record.id)
                    record = written;
        for (const Workspace::RegionRecord& written : applied)
            if (std::none_of(replaced.begin(), replaced.end(), [&written](const auto& r) { return r.id == written.id; }))
                stored.push_back(written);
        m_product_state->set_regions(std::move(stored));
        const auto snapshot = m_workspace.snapshot();
        json       rows     = json::array();
        for (const Workspace::RegionRecord& written : applied)
            rows.push_back(region_result(written, nullptr));
        activity.result_json = json{{"regions", std::move(rows)}, {"projectUndo", true},
                                    {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}}
                                   .dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::ObjectAnalyze) {
        const auto arguments = json::parse(activity.arguments_json);
        const auto object    = parse_object_argument(activity.arguments_json);
        Workspace::AnalysisRequest request;
        for (const auto& section : arguments["include"]) {
            request.mesh     = request.mesh || section == "mesh";
            request.features = request.features || section == "features";
            request.fit      = request.fit || section == "fit";
            request.orientations = request.orientations || section == "orientations";
        }
        const bool regions = std::find(arguments["include"].begin(), arguments["include"].end(), "regions") != arguments["include"].end();
        if (regions && m_product_state == nullptr) {
            fail(activity, "unavailable_operation", "Region annotations are not available here.");
            return;
        }
        for (const auto& candidate : arguments.value("candidates", json::array())) {
            Workspace::OrientationCandidate row;
            if (candidate.contains("up"))
                row.up = Workspace::Vec3{candidate["up"][0].get<double>(), candidate["up"][1].get<double>(), candidate["up"][2].get<double>()};
            else
                row.face_down = candidate["faceDown"].get<std::string>();
            request.candidates.push_back(std::move(row));
        }
        if (arguments.contains("measure"))
            request.measure = std::make_pair(arguments["measure"]["from"].get<std::string>(), arguments["measure"]["to"].get<std::string>());
        Workspace::ObjectAnalysis analysis;
        const auto analyzed = object ? m_workspace.analyze_object(*object, request, analysis) :
                                       Workspace::CommandResult::failure(Workspace::WorkspaceError::InvalidId, "Object ID is invalid");
        if (!analyzed.succeeded()) {
            fail(activity, workspace_error_code(analyzed.error), analyzed.message);
            return;
        }
        json result = object_analysis_result(*object, analysis, m_workspace.snapshot());
        if (regions) {
            json items     = json::array();
            bool truncated = false;
            for (const Workspace::RegionStatus& status : m_workspace.region_status(m_product_state->regions())) {
                // A record whose object cannot be found any more is shown on the
                // object it was written for, if that is this one.
                const bool here = status.object ? *status.object == *object :
                                                  status.record.session == object->session().value() && status.record.object == object->value();
                if (!here)
                    continue;
                if (items.size() == Workspace::kRegionLimit) {
                    truncated = true;
                    break;
                }
                items.push_back(region_result(status.record, &status));
            }
            result["regions"] = {{"items", std::move(items)}, {"truncated", truncated}};
        }
        activity.result_json = result.dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::PrinterSetupPreview) {
        const auto preview = m_workspace.preview_printer_setup(setup_request(json::parse(activity.arguments_json)));
        const auto devices = m_workspace.printers();
        json mismatches = json::array();
        if (preview.valid) {
            const auto facts = m_product_state != nullptr && m_product_state->has_printer_facts() ?
                                   m_product_state->printer_facts(printer_fact_key(preview.resulting, devices)) :
                                   std::vector<Workspace::PrinterFact>{};
            mismatches = printer_section_result(preview.resulting, devices, facts)["mismatches"];
        }
        activity.result_json = printer_setup_preview_result(preview, mismatches, m_workspace.snapshot()).dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::PrinterSetup) {
        const auto arguments = json::parse(activity.arguments_json);
        const Workspace::PrinterSetupRequest request = setup_request(arguments);
        Workspace::PrinterSetupPreview applied;
        if (!request.empty()) {
            if (setup_outcome(m_workspace.preview_printer_setup(request)) != arguments.at("confirmedSetup")) {
                fail(activity, "stale_workspace", "The presets changed since this setup was proposed. Preview it again.");
                return;
            }
            const auto result = m_workspace.apply_printer_setup(request, applied);
            if (!result.succeeded()) {
                fail(activity, workspace_error_code(result.error), result.message);
                return;
            }
        } else {
            applied.resulting = m_workspace.configured_printer();
        }
        const auto devices = m_workspace.printers();
        const std::string key = printer_fact_key(applied.resulting, devices);
        if (arguments.contains("confirmFacts")) {
            std::vector<Workspace::FactConfirmation> confirmations;
            for (const auto& fact : arguments["confirmFacts"])
                confirmations.push_back({fact["fact"].get<std::string>(), fact["value"].get<std::string>(),
                                         std::chrono::hours(fact.value("hours", std::uint64_t(24)))});
            m_product_state->confirm_printer_facts(key, confirmations);
        }
        const auto facts = m_product_state != nullptr && m_product_state->has_printer_facts() ?
                               m_product_state->printer_facts(key) : std::vector<Workspace::PrinterFact>{};
        const auto snapshot  = m_workspace.snapshot();
        activity.result_json = json{{"printer", printer_section_result(m_workspace.configured_printer(), devices, facts)},
                                    {"processPreset", m_workspace.current_process_preset()},
                                    {"substituted", setup_substitutions_result(applied.substitutions)},
                                    {"discarded", setup_edits_result(applied.unsaved_edits)},
                                    {"sessionId", std::to_string(snapshot.session.value())},
                                    {"revision", snapshot.revision}}
                                   .dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::PrinterList) {
        const auto snapshot = m_workspace.snapshot();
        bool       truncated = false;
        json       items     = json::array();
        for (const Workspace::PrinterDevice& device : m_workspace.printers()) {
            if (items.size() == 25) { truncated = true; break; }
            items.push_back(printer_device_result(device));
        }
        activity.result_json = json{{"items", std::move(items)}, {"truncated", truncated},
                                    {"sessionId", std::to_string(snapshot.session.value())},
                                    {"revision", snapshot.revision}}
                                   .dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::PresetsList) {
        const auto arguments = json::parse(activity.arguments_json);
        const std::string& kind = arguments.at("kind").get_ref<const std::string&>();
        Workspace::PresetQuery query;
        query.kind = kind == "printer" ? Workspace::PresetKind::Printer :
                     kind == "filament" ? Workspace::PresetKind::Filament : Workspace::PresetKind::Process;
        query.text            = arguments.value("query", "");
        query.compatible_only = arguments.value("compatibleOnly", true);
        query.limit           = arguments.value("limit", std::size_t(25));
        query.cursor          = arguments.value("cursor", "");
        activity.result_json  = presets_list_result(m_workspace.list_presets(query), m_workspace.snapshot()).dump();
        activity.state        = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::SliceReportRead) {
        const auto arguments = json::parse(activity.arguments_json);
        const auto snapshot  = m_workspace.snapshot();
        // The active plate is what "check this print" means when the caller
        // names none, and the name promises the current one.
        std::optional<Workspace::PlateId> plate = snapshot.active_plate;
        if (arguments.contains("plateId"))
            plate = Workspace::PlateId(snapshot.session, std::stoull(arguments["plateId"].get<std::string>()));
        if (!plate) {
            fail(activity, "unavailable_operation", "This project has no plate to report on.");
            return;
        }
        SliceReportSections sections;
        if (arguments.contains("sections")) {
            const auto asked = [&arguments](const char* name) {
                const auto& sections = arguments["sections"];
                return std::any_of(sections.begin(), sections.end(),
                                   [name](const json& value) { return value == name; });
            };
            sections = {asked("summary"), asked("findings"), asked("material"), asked("supports"),
                        asked("seams"),   asked("firstLayer"), asked("islands"),  asked("intent")};
        }
        if (sections.intent && m_product_state == nullptr) {
            fail(activity, "unavailable_operation", "This build cannot read the print intent.");
            return;
        }
        Workspace::SliceReportRequest request;
        request.supports    = sections.supports;
        request.seams       = sections.seams;
        request.first_layer = sections.first_layer;
        request.islands     = sections.islands;
        // The support and seam checks name the regions they cross.
        if ((request.supports || request.seams) && m_product_state != nullptr)
            request.regions = m_product_state->regions();
        const Workspace::SliceReport report = m_workspace.slice_report(*plate, request);
        json result = slice_report_result(report, *plate, snapshot, sections);
        if (sections.intent && report.valid) {
            std::vector<std::string> unchecked;
            const auto checks = check_intent(m_product_state->print_intent(), report.print_time_seconds, report.total_grams,
                                             report.has_cost ? std::optional<double>(report.total_cost) : std::nullopt, unchecked);
            json rows = json::array();
            for (const IntentCheck& check : checks)
                if (rows.size() < 32)
                    rows.push_back({{"field", check.field}, {"value", check.value}, {"kind", check.kind},
                                    {"limit", std::round(check.limit * 100) / 100}, {"actual", std::round(check.actual * 100) / 100},
                                    {"within", check.within}});
            if (unchecked.size() > 32)
                unchecked.resize(32);
            result["intent"] = {{"checks", std::move(rows)}, {"unchecked", unchecked}};
        }
        activity.result_json = result.dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::SliceStart) {
        const auto arguments = json::parse(activity.arguments_json);
        std::optional<Workspace::PlateId> plate;
        if (arguments.contains("plateId"))
            plate = Workspace::PlateId(Workspace::ProjectSessionId(activity.session),
                                       std::stoull(arguments["plateId"].get<std::string>()));
        const auto started = m_workspace.start_slice(plate, arguments.value("preempt", false));
        if (!started.succeeded()) {
            fail(activity, started.error == Workspace::WorkspaceError::UnavailableOperation ? "unavailable_operation" :
                           started.error == Workspace::WorkspaceError::StaleId ? "stale_id" : "invalid_id",
                 started.message);
            return;
        }
        // The run is Orca's, and it outlives this call: the handle is how the
        // caller finds it again in the slicing section, which is where the
        // result appears when the slicer is done.
        m_slice_handle       = activity.action_id;
        // The run may already be under way (and seen) when start returns.
        m_slice_seen_running = m_workspace.snapshot().slicing.running;
        m_slice_ended        = false;
        if (arguments.value("wait", false)) {
            m_slice_waits[activity.action_id] = {plate, false, std::chrono::steady_clock::now()};
            return;
        }
        const auto snapshot  = m_workspace.snapshot();
        activity.result_json = json{{"handle", activity.action_id}, {"started", true},
                                    {"slicing", slicing_section_result(snapshot, m_slice_handle)},
                                    {"sessionId", std::to_string(snapshot.session.value())},
                                    {"revision", snapshot.revision}}
                                   .dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::IntentUpdate || definition->handler == ToolHandler::PlanSet) {
        if (m_product_state == nullptr) {
            fail(activity, "unavailable_operation", "This build cannot record the intent or the plan.");
            return;
        }
        const auto arguments = json::parse(activity.arguments_json);
        const auto snapshot  = m_workspace.snapshot();
        json       result{{"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision},
                          // Neither record is in Orca's undo stack: say so where
                          // every other mutation says it.
                          {"projectUndo", false}};
        if (definition->handler == ToolHandler::IntentUpdate) {
            std::vector<IntentField> fields;
            for (const auto& field : arguments.at("fields")) {
                IntentField written;
                written.field    = field.at("field").get<std::string>();
                written.value    = field.value("value", "");
                written.question = field.value("question", "");
                // The card showed the interpreted answer and the user approved
                // it, so an answer is confirmed unless the agent says it only
                // assumed it.
                written.provenance = field.value("assumed", false) || written.value.empty() ?
                    Provenance::AgentInferred : Provenance::UserConfirmed;
                fields.push_back(std::move(written));
            }
            result["intent"] = intent_section_result(m_product_state->set_print_intent(fields));
        } else {
            PlanRecord plan;
            plan.headline = arguments.at("headline").get<std::string>();
            for (const auto& decision : arguments.value("decisions", json::array()))
                plan.decisions.push_back({decision.at("topic").get<std::string>(),
                                          decision.at("statement").get<std::string>(),
                                          decision.value("confidence", ""), decision.value("alternative", "")});
            for (const auto& line : arguments.value("assumptions", json::array()))
                plan.assumptions.push_back(line.get<std::string>());
            for (const auto& line : arguments.value("risks", json::array()))
                plan.risks.push_back(line.get<std::string>());
            result["plan"] = plan_section_result(m_product_state->set_plan(std::move(plan)));
        }
        activity.result_json = result.dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::SettingsSearch) {
        const auto args = json::parse(activity.arguments_json);
        Workspace::SettingsQuery query;
        query.text          = args.at("query").get<std::string>();
        query.limit         = args.value("limit", std::size_t(10));
        query.cursor        = args.value("cursor", "");
        query.writable_only = args.value("writable", false);
        query.changed_only  = args.value("changedOnly", false);
        const auto result = m_workspace.search_settings(query);
        if (result.error) {
            fail(activity, result.error->code, result.error->message, setting_issue_result(*result.error).dump());
            return;
        }
        activity.result_json = settings_search_result(result, m_workspace.snapshot()).dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::SettingsGet) {
        const auto args = json::parse(activity.arguments_json);
        const auto target = settings_target(args, m_workspace.snapshot());
        if (!target.found) {
            fail(activity, "missing_object", kMissingTarget);
            return;
        }
        const auto result = m_workspace.read_settings(args.at("keys").get<std::vector<std::string>>(), target.target);
        if (result.error) {
            fail(activity, result.error->code, result.error->message, setting_issue_result(*result.error).dump());
            return;
        }
        activity.result_json = settings_read_result(result, m_workspace.snapshot()).dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::SettingsPreviewPatch) {
        const auto args   = json::parse(activity.arguments_json);
        const auto target = settings_target(args, m_workspace.snapshot());
        if (!target.found) {
            fail(activity, "missing_object", kMissingTarget);
            return;
        }
        const auto result = m_workspace.preview_settings(settings_patch(args, target.target));
        if (!result.issues.empty() && result.issues.front().code == "workspace_unavailable") {
            fail(activity, result.issues.front().code, result.issues.front().message);
            return;
        }
        activity.result_json = settings_preview_result(result, m_workspace.snapshot()).dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::SettingsApplyPatch) {
        const auto args = json::parse(activity.arguments_json);
        const auto before = m_workspace.snapshot();
        // A plan's earlier members may have changed settings since this patch
        // was read; its confirmed values still guard every key it writes.
        if (!settings_revision_holds(args, before, activity.plan_id.empty() ? m_last_settings_revision : 0)) {
            fail(activity, "stale_workspace", "The workspace changed. Read and preview again.", stale_settings_details(args, before).dump());
            return;
        }
        std::vector<Workspace::SettingChange> confirmed;
        for (const auto& change : args.at("confirmedChanges"))
            confirmed.push_back({change.at("key"), change.at("before"), change.at("after")});
        // The revision check above means the target cannot have gone.
        const auto target = settings_target(args, before);
        Workspace::SettingsPreview applied;
        const auto result = m_workspace.apply_settings(settings_patch(args, target.target), confirmed, applied);
        if (result.error == WorkspaceError::StaleSettings) {
            fail(activity, "stale_workspace", result.message, stale_settings_details(args, m_workspace.snapshot()).dump());
            return;
        }
        if (result.error == WorkspaceError::InvalidSettings) {
            const auto& issue = applied.issues.at(0);
            fail(activity, issue.code, issue.message, settings_preview_result(applied, m_workspace.snapshot()).dump());
            return;
        }
        if (!result.succeeded() && result.error != WorkspaceError::NoChange) {
            fail(activity, workspace_error_code(result.error), result.message);
            return;
        }
        activity.result_json =
            settings_apply_result(applied, m_workspace.snapshot(), result.succeeded(), target.target.object.has_value()).dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::RecordBuild || definition->handler == ToolHandler::RecordExportCopy ||
        definition->handler == ToolHandler::RecordPhysicalPrint) {
        if (!m_extension_executor) {
            fail(activity, "execution_failed", "The registered tool executor is unavailable.");
            return;
        }
        ExtensionResult result = m_extension_executor(definition->handler, activity);
        if (result.handled) {
            if (result.error) {
                fail(activity, std::move(result.error->code), std::move(result.error->message));
                return;
            }
            activity.result_json = std::move(result.result_json);
            activity.state       = ToolState::Succeeded;
            notify(activity);
            return;
        }
        fail(activity, "execution_failed", "The registered tool executor did not handle the request.");
        return;
    }

    fail(activity, "execution_failed", "The registered tool has no executable handler.");
}

std::set<std::string> ToolExecutionCoordinator::bound_regions() const
{
    std::set<std::string> bound;
    if (m_product_state != nullptr)
        for (const Workspace::RegionStatus& status : m_workspace.region_status(m_product_state->regions()))
            if (status.object && !status.binding_lost)
                bound.insert(status.record.id);
    return bound;
}

json ToolExecutionCoordinator::regions_unbound(const std::set<std::string>& bound_before) const
{
    const std::set<std::string> bound = bound_regions();
    json                        lost  = json::array();
    for (const std::string& id : bound_before)
        if (!bound.count(id))
            lost.push_back(id);
    return lost;
}

void ToolExecutionCoordinator::fail(ToolActivity& activity, std::string code, std::string message, std::string details_json)
{
    activity.state = ToolState::Failed;
    activity.error = ToolError{std::move(code), std::move(message), std::move(details_json)};
    notify(activity);
    // A plan stops at its first failure: the members approved after it
    // do not run.
    if (!activity.plan_id.empty()) {
        const std::string plan = activity.plan_id, failed = activity.action_id;
        std::vector<std::string> rest;
        for (const ToolActivity& member : m_activities)
            if (member.plan_id == plan && member.action_id != failed &&
                (member.state == ToolState::Approved || member.state == ToolState::Running))
                rest.push_back(member.action_id);
        for (const std::string& id : rest)
            if (ToolActivity* member = find_mutable(id); member != nullptr && !tool_state_terminal(member->state))
                fail(*member, "plan_step_failed", "An earlier call of this plan failed, so this one did not run.");
    }
}

void ToolExecutionCoordinator::notify(const ToolActivity& activity)
{
    std::shared_ptr<ObserverState> state = m_observers;
    std::vector<std::uint64_t> ids;
    ids.reserve(state->observers.size());
    for (const auto& observer : state->observers)
        ids.push_back(observer.first);

    // Look each observer up immediately before invocation so callbacks may
    // safely unsubscribe themselves or another observer during dispatch.
    for (const std::uint64_t id : ids) {
        if (!state->alive)
            break;
        const auto observer = state->observers.find(id);
        if (observer == state->observers.end())
            continue;
        ActivityCallback callback = observer->second;
        callback(activity);
    }
}

void ToolExecutionCoordinator::invalidate_pending(const Workspace::WorkspaceChanged& change)
{
    // Pending proposals fail eagerly. Approved/running proposals recheck this
    // revision at execution, without invalidating a command on its own event.
    if ((change.reasons & kSettingsReasons) != WorkspaceChangeReasons::None)
        m_last_settings_revision = change.revision;
    if ((change.reasons & (kInvalidatingReasons | kSettingsReasons)) != WorkspaceChangeReasons::None) {
        const ToolActivity* executing = m_executing.empty() ? nullptr : find(m_executing);
        for (const ToolActivity& activity : m_activities)
            if (!activity.plan_id.empty() && (activity.state == ToolState::Approved || activity.state == ToolState::Running) &&
                (executing == nullptr || executing->plan_id != activity.plan_id))
                m_disturbed_plans.insert(activity.plan_id);
    }
    if ((change.reasons & kInvalidatingReasons) == WorkspaceChangeReasons::None)
        return;
    m_last_invalidating_revision = change.revision;
    for (ToolActivity& activity : m_activities) {
        if (activity.state != ToolState::Pending)
            continue;
        if (settings_patch_tool(activity.tool) && (change.reasons & kSettingsReasons) == WorkspaceChangeReasons::None)
            continue;
        fail(activity, "stale_revision", "The project changed after this action was proposed. Ask the Agent again.");
    }
}

} // namespace Slic3r::GUI::JusPrin::Agent
