#include "ToolExecutionCoordinator.hpp"
#include "Base64.hpp"
#include "ToolErrors.hpp"
#include "ToolExecution.hpp"
#include "ToolResults.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Regions.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SettingDisplay.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SettingsSupport.hpp"
#include "slic3r/GUI/JusPrin/Workspace/StoredFormat.hpp"
#include "slic3r/GUI/JusPrin/Workspace/UtcTime.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "libslic3r/format.hpp"
// Last: it defines the L marker.
#include "ActivityTitle.hpp"
#include "slic3r/GUI/JusPrin/Support/Translate.hpp"

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

using nlohmann::json;
using Workspace::WorkspaceChangeReasons;
using Workspace::WorkspaceError;

constexpr const char* kServerName = "jusprin-native";

// Workspace changes that can invalidate a queued action. Selection-only
// changes do not: an action pins its target by ID, so what the user selects
// afterwards cannot redirect it.
constexpr WorkspaceChangeReasons kInvalidatingReasons = WorkspaceChangeReasons::Contents | WorkspaceChangeReasons::Transform |
                                                        WorkspaceChangeReasons::Plates | WorkspaceChangeReasons::History |
                                                        WorkspaceChangeReasons::Project | WorkspaceChangeReasons::Settings;

// A settings patch is bound to the values it previewed, which the apply
// checks again; only a settings edit or a new project makes it stale. Model
// edits, and the slicing and selection events that follow them, do not.
constexpr WorkspaceChangeReasons kSettingsReasons = WorkspaceChangeReasons::Settings | WorkspaceChangeReasons::Project;

bool settings_patch_tool(const std::string& tool) { return tool == "settings_apply_patch"; }

bool exports_slice(const ToolActivity& activity)
{
    if (activity.tool != "export_file")
        return false;
    const std::string kind = json::parse(activity.arguments_json).value("kind", "");
    return kind == "gcode" || kind == "sliced_3mf";
}

// Whether a settings patch's preview still holds: same session, and no
// settings change after the revision it was previewed at.
bool settings_revision_holds(const json& arguments, const Workspace::WorkspaceSnapshot& snapshot, std::uint64_t settings_revision)
{
    return arguments.at("expectedSessionId") == std::to_string(snapshot.session.value()) &&
           arguments.at("expectedRevision").is_number_unsigned() &&
           arguments.at("expectedRevision").get<std::uint64_t>() <= snapshot.revision &&
           arguments.at("expectedRevision").get<std::uint64_t>() >= settings_revision;
}

// The one code each kind of failure reports, from Agent/ToolErrors.hpp.
const char* workspace_error_code(WorkspaceError error)
{
    switch (error) {
    case WorkspaceError::InvalidId:
    case WorkspaceError::NotFound: return "not_found";
    case WorkspaceError::StaleId:
    case WorkspaceError::StaleSettings: return "stale_workspace";
    case WorkspaceError::Busy: return "busy";
    case WorkspaceError::NeedsStep: return "needs_step";
    case WorkspaceError::Refused: return "refused";
    case WorkspaceError::Failed: return "failed";
    case WorkspaceError::WorkspaceUnavailable: return "workspace_unavailable";
    case WorkspaceError::InvalidArgument: return "invalid_arguments";
    case WorkspaceError::FeatureExpired: return "feature_expired";
    case WorkspaceError::None:
    case WorkspaceError::NoChange:
    case WorkspaceError::InvalidSettings: break;
    }
    // Settings apply turns NoChange into a result and InvalidSettings into
    // the issue's own code; no other tool command returns either.
    throw std::logic_error("Workspace error " + std::to_string(int(error)) + " is not a tool failure");
}

// Every tool that calls this requires sessionId and objectId as unsigned
// numbers that fit, so nullopt means the contract check let a bad call through.
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

std::vector<Workspace::ShapeRequest> shape_requests(const json& arguments)
{
    const auto vec = [](const json& value) {
        return Workspace::Vec3{value[0].get<double>(), value[1].get<double>(), value[2].get<double>()};
    };
    std::vector<Workspace::ShapeRequest> requests;
    for (const json& row : arguments["shapes"]) {
        Workspace::ShapeRequest request;
        request.type         = row["type"];
        request.shape.type   = row["shape"];
        request.shape.center = vec(row["center"]);
        if (row.contains("sizeMm")) request.shape.size = vec(row["sizeMm"]);
        if (row.contains("axis")) request.shape.normal = vec(row["axis"]);
        request.shape.diameter = row.value("diameterMm", 0.0);
        request.shape.length   = row.value("lengthMm", 0.0);
        requests.push_back(std::move(request));
    }
    return requests;
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
    return request;
}

// Only actions that can change the printable scene need a pinned before
// checkpoint and a before/after model-version link. The other mutations are
// durable document records or external printer/file actions.
bool changes_printable_project(const ToolDefinition& definition, const ToolActivity& activity)
{
    switch (definition.handler) {
    case ToolHandler::SettingsApplyPatch:
    case ToolHandler::ObjectImport:
    case ToolHandler::ObjectImportFile:
    case ToolHandler::ProjectDeleteItems:
    case ToolHandler::PlateLayout:
    case ToolHandler::ObjectPlace:
    case ToolHandler::ObjectMarkRegion:
    case ToolHandler::ObjectDivide:
    case ToolHandler::ObjectMerge:
    case ToolHandler::ObjectRepair:
    case ToolHandler::ObjectAddShape:
    case ToolHandler::PrinterChange: // drops unsaved edits of the printer in use
        return true;
    case ToolHandler::PrinterSetup:
        return !setup_request(json::parse(activity.arguments_json)).empty();
    case ToolHandler::WorkspaceInspect:
    case ToolHandler::SettingsSearch:
    case ToolHandler::SettingsGet:
    case ToolHandler::SettingsPreviewPatch:
    case ToolHandler::SetupRecord:
    case ToolHandler::PresetsList:
    case ToolHandler::ObjectAnalyze:
    case ToolHandler::ObjectDividePreview:
    case ToolHandler::ViewRender:
    case ToolHandler::AttachmentRead:
    case ToolHandler::SliceInspect:
    case ToolHandler::ActivityCancel:
    case ToolHandler::ExportFile:
    case ToolHandler::PrinterSetupPreview:
    case ToolHandler::ProjectOpen:
    case ToolHandler::CalibrationGenerate: // saves the project it closes, as an open does
    case ToolHandler::PrinterList:
    case ToolHandler::SliceStart:
    case ToolHandler::PrintDialogOpen: // shows a dialog; changes no project state
    case ToolHandler::SliceReportRead:
    case ToolHandler::PrinterIdentify:
    case ToolHandler::PrinterAdd:
    case ToolHandler::PrinterConnectionStatus:
    case ToolHandler::PrinterHostConnect:
    case ToolHandler::PrinterBambuConnect:
    case ToolHandler::IssueReportOpen: // opens a browser page; the person files it
    case ToolHandler::SkillRead:
        return false;
    }
    throw std::logic_error("tool handler has no checkpoint classification");
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
// when the call names an object the project does not have; a preset is
// looked up by the workspace, by its name.
struct SettingsTargetLookup
{
    Workspace::SettingsTarget target;
    std::string               object_name;
    bool                      found{true};
};

SettingsTargetLookup settings_target(const json& arguments, const Workspace::WorkspaceSnapshot& snapshot)
{
    using Workspace::SettingsScope;
    SettingsTargetLookup result;
    const std::string scope = arguments.at("scope");
    result.target.scope     = scope == "object"   ? SettingsScope::Object :
                              scope == "filament" ? SettingsScope::Filament :
                              scope == "printer"  ? SettingsScope::Printer :
                                                    SettingsScope::Process;
    if (result.target.scope == SettingsScope::Filament || result.target.scope == SettingsScope::Printer)
        result.target.preset = arguments.at("target").at("preset");
    if (result.target.scope != SettingsScope::Object)
        return result;
    const std::string id = arguments.at("target").at("objectId");
    for (const auto& plate : snapshot.plates)
        for (const auto& object : plate.objects)
            if (std::to_string(object.id.value()) == id) {
                result.target.object = object.id;
                result.object_name   = object.name;
            }
    result.found = result.target.object.has_value();
    return result;
}

constexpr const char* kMissingTarget = "No plate in the open project holds that object. Read workspace_inspect again.";
constexpr const char* kPlateNotFound = "That plate is not in the open project. Read workspace_inspect for the plate ids.";
constexpr const char* kProjectsUnreadable =
    "Nothing was opened: JusPrin could not read its saved projects to check whether this file was opened before. "
    "Tell the person; the file can be opened once JusPrin can read its projects folder again.";

Workspace::SettingsPatch settings_patch(const json& arguments, const Workspace::SettingsTarget& target)
{
    Workspace::SettingsPatch patch{arguments.at("changes").get<std::map<std::string, std::string>>(), target};
    if (arguments.contains("persistAs"))
        patch.persist_as = arguments["persistAs"].get<std::string>();
    return patch;
}

// The activity title: what changes, whose, and where it is saved.
ActivityTitle settings_title(const Workspace::SettingsPreview& preview, const SettingsTargetLookup& target,
                             const Workspace::SettingsPatch& patch)
{
    const std::string whose = target.target.object ? target.object_name : "\"" + preview.preset + "\"";
    std::string       keys;
    for (std::size_t index = 0; index < preview.changes.size(); ++index)
        keys += (index == 0 ? "" : ", ") + preview.changes[index].key;
    const std::size_t count = preview.changes.size();
    if (!patch.persist_as)
        // TRN %1% is how many settings, %2% an object's name or a preset's name in quotes, %3% the settings' keys.
        return plural_title("Change %1% setting of %2%: %3%", "Change %1% settings of %2%: %3%", count, whose, keys);
    if (*patch.persist_as == preview.preset)
        // TRN %1% is how many settings, %2% a preset's name in quotes, %3% the settings' keys. The preset is saved with them.
        return plural_title("Change %1% setting of %2%: %3%; save it", "Change %1% settings of %2%: %3%; save it", count, whose, keys);
    // TRN %1% is how many settings, %2% a preset's name in quotes, %3% the settings' keys, %4% the new preset's name.
    return plural_title("Change %1% setting of %2%: %3%; save as \"%4%\"", "Change %1% settings of %2%: %3%; save as \"%4%\"", count,
                        whose, keys, *patch.persist_as);
}

json stale_settings_details(const json& arguments, const Workspace::WorkspaceSnapshot& snapshot)
{
    return {{"expectedSessionId", arguments.at("expectedSessionId")}, {"expectedRevision", arguments.at("expectedRevision")},
            {"currentSessionId", std::to_string(snapshot.session.value())}, {"currentRevision", snapshot.revision}};
}

// The request a calibration_generate call makes, its dialog's defaults not
// yet filled in.
Workspace::CalibrationRequest calibration_request(const json& arguments)
{
    using Workspace::CalibrationTest;
    using Workspace::PressureAdvanceMethod;
    Workspace::CalibrationRequest request;
    const std::string test = arguments.at("test").get<std::string>();
    request.test = test == "flow" ? CalibrationTest::Flow : test == "pressure_advance" ? CalibrationTest::PressureAdvance :
                                                                                         CalibrationTest::Temperature;
    if (arguments.contains("start")) request.start = arguments["start"].get<double>();
    if (arguments.contains("end")) request.end = arguments["end"].get<double>();
    if (arguments.contains("step")) request.step = arguments["step"].get<double>();
    const std::string method = arguments.value("method", "tower");
    request.method      = method == "line" ? PressureAdvanceMethod::Line : method == "pattern" ? PressureAdvanceMethod::Pattern :
                                                                                               PressureAdvanceMethod::Tower;
    request.bowden      = arguments.value("extruderType", "direct_drive") == "bowden";
    request.flow_pass   = arguments.value("pass", 1);
    request.flow_linear = arguments.value("linear", true);
    return request;
}

// Unrounded: a pressure-advance step is a few thousandths.
std::string calibration_number(double value)
{
    std::ostringstream out;
    out << value;
    return out.str();
}

const char* pa_method_name(Workspace::PressureAdvanceMethod method)
{
    return method == Workspace::PressureAdvanceMethod::Line    ? "line" :
           method == Workspace::PressureAdvanceMethod::Pattern ? "pattern" : "tower";
}

ActivityTitle calibration_title(const Workspace::CalibrationRequest& request)
{
    switch (request.test) {
    case Workspace::CalibrationTest::Temperature:
        // TRN %1% and %2% are the tower's first and last temperatures.
        return title_of(L("Make a temperature tower, %1% °C to %2% °C"), calibration_number(*request.start),
                        calibration_number(*request.end));
    case Workspace::CalibrationTest::Flow:
        // TRN YOLO is the name of OrcaSlicer's quicker flow-ratio test; %1% is its pass, 1 or 2.
        return request.flow_linear ? title_of(L("Make a flow-ratio test, YOLO pass %1%"), request.flow_pass) :
                                     // TRN %1% is the test's pass, 1 or 2.
                                     title_of(L("Make a flow-ratio test, classic pass %1%"), request.flow_pass);
    case Workspace::CalibrationTest::PressureAdvance: {
        const std::string start = calibration_number(*request.start), end = calibration_number(*request.end);
        // TRN %1% and %2% are the first and last pressure-advance values.
        return request.method == Workspace::PressureAdvanceMethod::Line    ? title_of(L("Make a pressure-advance line, %1% to %2%"), start, end) :
               // TRN %1% and %2% are the first and last pressure-advance values.
               request.method == Workspace::PressureAdvanceMethod::Pattern ? title_of(L("Make a pressure-advance pattern, %1% to %2%"), start, end) :
                                                                             // TRN %1% and %2% are the first and last pressure-advance values.
                                                                             title_of(L("Make a pressure-advance tower, %1% to %2%"), start, end);
    }
    }
    throw std::logic_error("calibration test has no title");
}

// The sentence the test's project opens its chat with: what was made, in the
// person's terms and the app's own voice, from what Orca reports it applies.
// The word for what the person picks on the print of the test that measures
// a setting (board 5b's {step}), by the setting a result writes: a block of
// the temperature tower, a flow tile, or a pressure-advance height, line or
// corner by the active plate's method. None for a setting no test measures.
// The proposal's second button, for each part calibration_pick names.
const char* pick_another(const std::string& pick)
{
    // TRN A button that asks the AI to choose another block of a calibration test.
    if (pick == "block") return L("Pick another block");
    // TRN A button that asks the AI to choose another tile of a calibration test.
    if (pick == "tile") return L("Pick another tile");
    // TRN A button that asks the AI to choose another line of a calibration test.
    if (pick == "line") return L("Pick another line");
    // TRN A button that asks the AI to choose another corner of a calibration test.
    if (pick == "corner") return L("Pick another corner");
    // TRN A button that asks the AI to choose another height of a calibration tower.
    return L("Pick another height");
}

std::string calibration_pick(const std::string& key, const Workspace::WorkspaceSnapshot& snapshot)
{
    if (key == "nozzle_temperature")
        return "block";
    if (key == "filament_flow_ratio")
        return "tile";
    if (key != "pressure_advance")
        return {};
    const auto plate = std::find_if(snapshot.plates.begin(), snapshot.plates.end(), [](const auto& item) { return item.active; });
    const std::string method = plate != snapshot.plates.end() && plate->calibration ? plate->calibration->method : "";
    return method == "line" ? "line" : method == "pattern" ? "corner" : "height";
}

// The card a preview that saves a preset is approved from (board 5b): every
// slot comes from the preview -- the setting with its saved and new values,
// the preset's scope, the name it is saved as -- and from the settings' own
// labels and units. A change that only puts a temporary edit back to its
// saved value is not part of the result and draws no row. Null when nothing
// would differ from the saved preset.
json settings_proposal(const Workspace::SettingsPreview& preview, const Workspace::SettingsTarget& target,
                       const std::string& persist_as, const std::vector<Workspace::SettingValue>& values,
                       const std::string& base, const Workspace::WorkspaceSnapshot& snapshot)
{
    const std::string scope = target.scope == Workspace::SettingsScope::Filament ? "filament" :
                              target.scope == Workspace::SettingsScope::Printer  ? "printer" :
                                                                                   "process";
    const bool in_place = persist_as == preview.preset;
    // The card is drawn and never read by the model, so its words are the
    // person's language; the receipt below stays English, for the note the
    // model reads once the preset is saved.
    // TRN %1% is a preset's name.
    const char* saved_as = in_place ? L("Preset · %1%") :
                                      // TRN %1% is a preset's name.
                                      L("New preset · %1%");
    json rows = json::array({json{{"icon", "save"}, {"text", Slic3r::format(translated(saved_as), persist_as)}}});
    std::string value, value_shown, saved_settings, saved_shown, pick;
    json        restore = json::object();
    for (const Workspace::SettingChange& change : preview.changes) {
        if (change.saved && *change.saved == change.after)
            continue;
        const auto known = std::find_if(values.begin(), values.end(), [&change](const auto& item) { return item.key == change.key; });
        Workspace::SettingDefinition definition;
        if (known != values.end())
            definition = known->definition;
        if (definition.label.empty())
            definition.label = change.key;
        const std::string after = Workspace::display_value(definition, change.after);
        // The label is OrcaSlicer's, which its own catalog translates.
        const std::string shown_label = translated(definition.label.c_str());
        rows.push_back({{"icon", change.key.find("temperature") != std::string::npos ? "thermometer" : "sliders"},
                        {"text", shown_label + " " +
                                     Workspace::display_change(definition, change.saved.value_or(change.before), change.after,
                                                               Workspace::ValueWords::Shown)}});
        std::string named = definition.label;
        named.front() = char(std::tolower(static_cast<unsigned char>(named.front())));
        saved_settings += (saved_settings.empty() ? "" : ", ") + named + " " + after;
        // Its name, then its value. The English name starts lower case, as
        // in the model's text; a translated one stays as its catalog writes
        // it (a German noun keeps its capital).
        saved_shown += (saved_shown.empty() ? "" : ", ") + (shown_label == definition.label ? named : shown_label) + " " +
                       Workspace::display_value(definition, change.after, Workspace::ValueWords::Shown);
        if (value.empty()) {
            value       = after;
            value_shown = Workspace::display_value(definition, change.after, Workspace::ValueWords::Shown);
            pick  = calibration_pick(change.key, snapshot);
        }
        restore[change.key] = change.saved.value_or(change.before);
    }
    if (value.empty())
        return nullptr;
    if (!in_place)
        // TRN %1% is the preset the new one is made from.
        rows.push_back({{"icon", "info"}, {"text", Slic3r::format(translated(L("%1% itself is unchanged")), base)}});
    std::string kind = scope;
    kind.front() = char(std::toupper(static_cast<unsigned char>(kind.front())));
    // TRN The saved preset's receipt: %1% lists the settings saved, such as "Nozzle temperature 215 °C".
    const char* receipt = target.scope == Workspace::SettingsScope::Filament ? L("Filament preset · %1%") :
                          // TRN The saved preset's receipt: %1% lists the settings saved.
                          target.scope == Workspace::SettingsScope::Printer  ? L("Printer preset · %1%") :
                                                                               // TRN The saved preset's receipt: %1% lists the settings saved.
                                                                               L("Process preset · %1%");
    // TRN %1% is a setting's new value, such as 215 °C.
    const char* as_new = target.scope == Workspace::SettingsScope::Filament ? L("Save %1% as a new filament preset") :
                         // TRN %1% is a setting's new value, such as 215 °C.
                         target.scope == Workspace::SettingsScope::Printer  ? L("Save %1% as a new printer preset") :
                                                                              // TRN %1% is a setting's new value, such as 215 °C.
                                                                              L("Save %1% as a new process preset");
    // TRN %1% is a setting's new value, such as 215 °C, %2% a preset's name.
    json proposal{{"title", in_place ? Slic3r::format(translated(L("Save %1% to %2%")), value_shown, persist_as)
                                     : Slic3r::format(translated(as_new), value_shown)},
                  {"rows", std::move(rows)},
                  {"save", translated(L("Save preset"))},
                  {"value", value},
                  // Board 6's receipt, once it is saved: the model reads the
                  // English, the note shows the person's language.
                  {"receipt", kind + " preset · " + saved_settings},
                  {"receiptShown", Slic3r::format(translated(receipt), saved_shown)}};
    // The button's words are also the message it sends, as the person's own.
    if (!pick.empty())
        proposal["other"] = translated(pick_another(pick));
    // Board 6's Undo, for a filament: a copy is deleted and the preset it was
    // made from used again; a preset saved in place gets its values back.
    if (target.scope == Workspace::SettingsScope::Filament)
        proposal["undo"] = {{"base", preview.preset}, {"label", base}, {"newCopy", !in_place}, {"restore", std::move(restore)}};
    return proposal;
}

// The line the test's own chat opens with (board 4's system line): where it
// came from, and that its settings are temporary. What the test is, the
// model reads from the workspace: the plate's calibration and the objects.
// A preset by the name OrcaSlicer's menus show it under: its alias.
std::string preset_label(const Workspace::IWorkspace& workspace, Workspace::PresetKind kind, const std::string& name)
{
    const auto presets = workspace.list_presets({kind, name, false, 100, {}}).items;
    const auto entry   = std::find_if(presets.begin(), presets.end(), [&name](const auto& item) { return item.name == name; });
    return entry != presets.end() && !entry->label.empty() ? entry->label : name;
}

std::string calibration_summary(const Workspace::CalibrationRequest& request, const Workspace::CalibrationResult& made)
{
    const std::string in = " is ready in this project, \"" + made.project_name + "\": ";
    std::string text;
    switch (request.test) {
    case Workspace::CalibrationTest::Temperature:
        text = "The temperature tower" + in + "blocks from " + calibration_number(*made.start) +
               " °C at the bottom to " + calibration_number(*made.end) + " °C at the top, " + calibration_number(*made.step) +
               " °C apart.";
        break;
    case Workspace::CalibrationTest::Flow:
        text = std::string("The ") + (request.flow_linear ? "YOLO" : "classic") + " flow-ratio test, pass " +
               std::to_string(request.flow_pass) + "," + in + std::to_string(made.objects.size()) +
               " tiles, each named for its change to the flow ratio.";
        break;
    case Workspace::CalibrationTest::PressureAdvance:
        text = request.method == Workspace::PressureAdvanceMethod::Tower ?
                   "The pressure-advance tower" + in + "pressure advance from " + calibration_number(*made.start) +
                       " at the bottom to " + calibration_number(*made.end) + " at the top, rising " +
                       calibration_number(*made.step) + " per millimetre." :
                   std::string("The pressure-advance ") + pa_method_name(request.method) + " test" + in +
                       "pressure advance from " + calibration_number(*made.start) + " to " + calibration_number(*made.end) +
                       " in steps of " + calibration_number(*made.step) + ", numbered on the print.";
        break;
    }
    return text + " Its changes to the presets are for this test only and are not saved.";
}

} // namespace

ToolExecutionCoordinator::ToolExecutionCoordinator(Workspace::IWorkspace& workspace, const ToolRegistry& registry)
    : m_workspace(workspace), m_registry(registry), m_observers(std::make_shared<ObserverState>())
{
    m_workspace_subscription = m_workspace.subscribe([this](const Workspace::WorkspaceChanged& change) {
        // A run belongs to the project it started in; in another, its handle
        // names nothing and its end is nobody's to report.
        if (!m_slice_handle.empty() && change.session != m_slice_session)
            m_slice_handle.clear();
        // The end of the run a slice_start began is a workspace change like
        // any other.
        if (m_slice_running && !m_workspace.snapshot().slicing.running) {
            m_slice_running = false;
            if (m_slice_ended)
                m_slice_ended(m_slice_handle);
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

const ToolActivity& ToolExecutionCoordinator::submit(const ToolRequest& request, const std::string& correlation_id,
                                                     ToolExecutionPacing pacing, ToolSource source,
                                                     const std::string& call_id, const std::string& client)
{
    const Workspace::WorkspaceSnapshot snapshot = m_workspace.snapshot();
    const ToolDefinition* definition = m_registry.find(request.tool);

    ToolActivity activity;
    activity.action_id         = m_action_id_allocator ? m_action_id_allocator() : "t-" + std::to_string(m_next_action_id++);
    activity.correlation_id    = correlation_id;
    activity.call_id           = call_id;
    activity.source            = source;
    activity.approves          = m_approving;
    activity.client            = client;
    activity.server            = kServerName;
    activity.tool              = request.tool;
    activity.arguments_json    = request.arguments_json;
    activity.session           = snapshot.session.value();
    activity.expected_revision = snapshot.revision;
    activity.progress_total    = pacing.ticks > 0 ? pacing.ticks : 1;

    if (definition != nullptr) {
        set_title(activity, {definition->title, translated(definition->title.c_str())});
        activity.action_class      = definition->action_class;
    } else {
        set_title(activity, title_of(L("Unknown tool request")));
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
    set_title(stored, m_registry.activity_title(*definition, stored.arguments_json));

    if (definition->handler == ToolHandler::ObjectImportFile) {
        const auto arguments = json::parse(stored.arguments_json);
        // Validate the name before the file is read during execution.
        const std::filesystem::path path = std::filesystem::u8path(arguments["path"].get<std::string>());
        std::error_code error;
        if (!path.is_absolute() || !std::filesystem::is_regular_file(path, error)) {
            fail(stored, "invalid_arguments", "Give the absolute path of a file that exists.");
            return stored;
        }
        // TRN %1% is a file's path.
        set_title(stored, title_of(L("Import %1%"), arguments["path"].get<std::string>()));
    }

    // An object, plate or region the snapshot no longer has is named by its id.
    const auto object_title = [&snapshot](const std::string& id) {
        for (const auto& plate : snapshot.plates)
            for (const auto& object : plate.objects)
                if (std::to_string(object.id.value()) == id) return same_title(object.name);
        // TRN %1% is an object's id, a number.
        return title_of(L("object %1%"), id);
    };
    const auto name_of_object = [&object_title](const json& id) { return object_title(id.get<std::string>()); };
    const auto plate_title = [&snapshot](const std::string& id) {
        for (const auto& plate : snapshot.plates)
            if (std::to_string(plate.id.value()) == id) return same_title(plate.name);
        // TRN %1% is a plate's id, a number.
        return title_of(L("plate %1%"), id);
    };

    if (definition->handler == ToolHandler::ObjectDivide) {
        // The card says what the pieces will be, from the same dry run the
        // preview tool reports.
        const auto arguments = json::parse(stored.arguments_json);
        const auto object    = parse_object_argument(stored.arguments_json);
        const auto request   = divide_request(arguments);
        Workspace::DivideResult preview;
        const auto previewed = object ? m_workspace.preview_divide(*object, request, preview) :
                                        throw std::logic_error("A checked objectId did not parse");
        if (!previewed.succeeded()) {
            fail(stored, workspace_error_code(previewed.error), previewed.message);
            return stored;
        }
        const ActivityTitle name = name_of_object(arguments["objectId"]);
        std::vector<ActivityTitle> sizes;
        for (const auto& piece : preview.pieces)
            // TRN A piece's size: width, depth and height in millimetres.
            sizes.push_back(title_of(L("%1% x %2% x %3% mm"), number_words(piece.size[0]), number_words(piece.size[1]),
                                     number_words(piece.size[2])));
        const ActivityTitle listed = joined_titles(sizes);
        const std::size_t   count  = preview.pieces.size();
        ActivityTitle       title;
        if (request.mode == Workspace::DivideRequest::Mode::Shells) {
            title = request.as_parts ?
                        // TRN %1% is how many shells, %2% an object's name, %3% the pieces' sizes.
                        plural_title("Split %2% into its %1% shell as parts of one object: %3%",
                                     "Split %2% into its %1% shells as parts of one object: %3%", count, name, listed) :
                        // TRN %1% is how many shells, %2% an object's name, %3% the pieces' sizes.
                        plural_title("Split %2% into its %1% shell as separate objects: %3%",
                                     "Split %2% into its %1% shells as separate objects: %3%", count, name, listed);
        } else {
            const std::string point  = number_words(request.point[0]) + ", " + number_words(request.point[1]) + ", " +
                                       number_words(request.point[2]);
            const std::string normal = number_words(request.normal[0]) + ", " + number_words(request.normal[1]) + ", " +
                                       number_words(request.normal[2]);
            const char* words =
                request.keep_upper && request.keep_lower ?
                    (request.as_parts ?
                         // TRN %1% is an object's name, %2% and %3% a point and a direction as x, y, z, %4% the pieces' sizes.
                         L("Cut %1% by the plane through (%2%) facing (%3%), keeping both pieces as parts: %4%") :
                         // TRN %1% is an object's name, %2% and %3% a point and a direction as x, y, z, %4% the pieces' sizes.
                         L("Cut %1% by the plane through (%2%) facing (%3%), keeping both pieces: %4%")) :
                request.keep_upper ?
                    (request.as_parts ?
                         // TRN %1% is an object's name, %2% and %3% a point and a direction as x, y, z, %4% the pieces' sizes.
                         L("Cut %1% by the plane through (%2%) facing (%3%), keeping the upper piece as parts: %4%") :
                         // TRN %1% is an object's name, %2% and %3% a point and a direction as x, y, z, %4% the pieces' sizes.
                         L("Cut %1% by the plane through (%2%) facing (%3%), keeping the upper piece: %4%")) :
                    (request.as_parts ?
                         // TRN %1% is an object's name, %2% and %3% a point and a direction as x, y, z, %4% the pieces' sizes.
                         L("Cut %1% by the plane through (%2%) facing (%3%), keeping the lower piece as parts: %4%") :
                         // TRN %1% is an object's name, %2% and %3% a point and a direction as x, y, z, %4% the pieces' sizes.
                         L("Cut %1% by the plane through (%2%) facing (%3%), keeping the lower piece: %4%"));
            title = title_of(words, name, point, normal, listed);
        }
        set_title(stored, title);
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
        std::error_code error;
        const bool replaces = std::filesystem::exists(std::filesystem::u8path(request.path), error) && request.kind != "presets";
        // TRN %1% is a file's path.
        const char* words = request.kind == "gcode"       ? (replaces ? L("Export the G-code to %1%, replacing it") : L("Export the G-code to %1%")) :
                            // TRN %1% is a file's path.
                            request.kind == "sliced_3mf"  ? (replaces ? L("Export the sliced plate to %1%, replacing it") : L("Export the sliced plate to %1%")) :
                            // TRN %1% is a file's path.
                            request.kind == "project_3mf" ? (replaces ? L("Export the project to %1%, replacing it") : L("Export the project to %1%")) :
                            // TRN %1% is a file's path.
                            request.kind == "stl"         ? (replaces ? L("Export an STL to %1%, replacing it") : L("Export an STL to %1%")) :
                                                            // TRN %1% is a file's path.
                                                            L("Export the presets in use to %1%");
        std::vector<ActivityTitle> parts{title_of(words, request.path)};
        const std::string license = m_workspace.project_details().license;
        if (license_restricts(license))
            // TRN %1% is a license's name, such as CC BY-NC.
            parts.push_back(title_of(L("the project's license is %1%"), license));
        const auto warnings = export_slice_warnings(m_workspace, request);
        if (!warnings.empty())
            // TRN %1% is how many warnings, %2% the first one, in OrcaSlicer's words.
            parts.push_back(plural_title("the slice has %1% warning: %2%", "the slice has %1% warnings, first: %2%", warnings.size(),
                                         warnings.front()));
        set_title(stored, joined_titles(parts, "; "));
    }

    if (definition->handler == ToolHandler::ObjectMerge) {
        const auto arguments = json::parse(stored.arguments_json);
        const auto& ids = arguments["objectIds"];
        std::vector<ActivityTitle> names;
        for (const auto& id : ids)
            names.push_back(name_of_object(id));
        const ActivityTitle last = names.back();
        names.pop_back();
        // TRN %1% is a list of objects' names, %2% the last name.
        set_title(stored, title_of(L("Merge %1% and %2% into one object"), joined_titles(names), last));
    }

    if (definition->handler == ToolHandler::ObjectRepair) {
        const auto arguments = json::parse(stored.arguments_json);
        // TRN %1% is an object's name.
        set_title(stored, title_of(L("Repair the mesh of %1%"), name_of_object(arguments["objectId"])));
    }

    if (definition->handler == ToolHandler::ObjectAddShape) {
        const auto arguments = json::parse(stored.arguments_json);
        std::vector<ActivityTitle> shapes;
        for (const Workspace::ShapeRequest& shape : shape_requests(arguments))
            shapes.push_back(shape.type == "part" ?
                                 // TRN %1% is a shape's size, such as "10 x 10 x 5 mm box", in English.
                                 title_of(L("a part (%1%)"), Workspace::region_geometry_words(shape.shape)) :
                                 // TRN %1% is a shape's size, such as "5 x 20 mm cylinder", in English. A negative part is a hole.
                                 title_of(L("a negative part (%1%)"), Workspace::region_geometry_words(shape.shape)));
        // TRN %1% is an object's name, %2% a list of the parts added to it.
        set_title(stored, title_of(L("Add to %1%: %2%"), name_of_object(arguments["objectId"]), joined_titles(shapes, "; ")));
    }

    if (definition->handler == ToolHandler::ObjectMarkRegion) {
        if (m_product_state == nullptr)
            throw std::logic_error("object_mark_region reached a coordinator with no product state");
        std::vector<Workspace::RegionRecord> planned;
        const auto plan = m_workspace.plan_regions(region_requests(json::parse(stored.arguments_json)), m_product_state->regions(), planned);
        if (!plan.succeeded()) {
            fail(stored, workspace_error_code(plan.error), plan.message);
            return stored;
        }
        // The regions' labels are the model's own description of each, in English.
        std::string regions;
        for (std::size_t index = 0; index < planned.size(); ++index)
            regions += (index == 0 ? "" : "; ") + planned[index].id + " " + planned[index].label;
        // TRN %1% is a list of regions, each its id and a description.
        set_title(stored, title_of(L("Annotate %1%"), regions));
    }

    if (definition->handler == ToolHandler::ProjectDeleteItems) {
        const auto arguments = json::parse(stored.arguments_json);
        std::vector<ActivityTitle> items;
        for (const auto& row : arguments["items"]) {
            if (row.contains("plateId")) {
                items.push_back(plate_title(row["plateId"].get<std::string>()));
            } else if (row.contains("regionId")) {
                const std::string id = row["regionId"].get<std::string>();
                std::string       label;
                if (m_product_state != nullptr)
                    for (const auto& region : m_product_state->regions())
                        if (region.id == id) label = region.label;
                items.push_back(label.empty() ?
                                    // TRN %1% is a region's id.
                                    title_of(L("region %1%"), id) :
                                    // TRN %1% is a region's id, %2% its description.
                                    title_of(L("region %1% (%2%)"), id, label));
            } else if (row.contains("partId")) {
                // TRN %1% is an object's name.
                items.push_back(title_of(L("a part of %1%"), object_title(row["objectId"].get<std::string>())));
            } else if (row.contains("instance")) {
                // TRN %1% is which copy, counting from 1, %2% an object's name.
                items.push_back(title_of(L("copy %1% of %2%"), row["instance"].get<std::size_t>() + 1,
                                         object_title(row["objectId"].get<std::string>())));
            } else {
                items.push_back(object_title(row["objectId"].get<std::string>()));
            }
        }
        // TRN %1% is a list of what is deleted: objects, copies, parts, plates or regions.
        set_title(stored, title_of(L("Delete %1%"), joined_titles(items)));
    }

    if (definition->handler == ToolHandler::PlateLayout) {
        const auto arguments = json::parse(stored.arguments_json);
        std::vector<ActivityTitle> parts;
        for (const auto& row : arguments.value("objects", json::array())) {
            const ActivityTitle name = object_title(row["objectId"].get<std::string>());
            // TRN %1% is an object's name, %2% how many copies it will have.
            if (row.contains("quantity")) parts.push_back(title_of(L("%1% x%2%"), name, row["quantity"].get<int>()));
            if (row.contains("enabled"))
                // TRN %1% is an object's name.
                parts.push_back(row["enabled"].get<bool>() ? title_of(L("print %1%"), name) :
                                                             // TRN %1% is an object's name.
                                                             title_of(L("skip %1%"), name));
            if (row.contains("plateId"))
                // TRN %1% is an object's name, %2% a plate's name.
                parts.push_back(title_of(L("move %1% to %2%"), name, plate_title(row["plateId"].get<std::string>())));
            if (row.contains("name"))
                // TRN %1% is an object's name, %2% its new name.
                parts.push_back(title_of(L("rename %1% to %2%"), name, row["name"].get<std::string>()));
            if (row.contains("extruder"))
                // TRN %1% is an object's name, %2% a filament slot's number.
                parts.push_back(title_of(L("%1% on filament %2%"), name, row["extruder"].get<int>()));
        }
        for (const auto& row : arguments.value("plates", json::array())) {
            const bool existing = row.contains("plateId");
            const ActivityTitle name = existing ? plate_title(row["plateId"].get<std::string>()) : ActivityTitle{};
            if (!existing) parts.push_back(title_of(L("add a plate")));
            if (row.contains("name"))
                parts.push_back(existing ?
                                    // TRN %1% is a plate's name, %2% its new name.
                                    title_of(L("name %1% %2%"), name, row["name"].get<std::string>()) :
                                    // TRN %1% is the new plate's name.
                                    title_of(L("name a new plate %1%"), row["name"].get<std::string>()));
            if (row.contains("bedType"))
                parts.push_back(existing ?
                                    // TRN %1% is a plate's name, %2% a build plate type, such as Textured PEI Plate.
                                    title_of(L("%1% on %2%"), name, row["bedType"].get<std::string>()) :
                                    // TRN %1% is a build plate type, such as Textured PEI Plate.
                                    title_of(L("a new plate on %1%"), row["bedType"].get<std::string>()));
        }
        if (arguments.contains("arrange"))
            parts.push_back(arguments["arrange"].contains("plateId") ?
                                // TRN %1% is a plate's name.
                                title_of(L("arrange %1%"), plate_title(arguments["arrange"]["plateId"].get<std::string>())) :
                                title_of(L("arrange all plates")));
        // TRN %1% is a list of changes to the plates, such as "move Cube to Plate 2, arrange all plates".
        set_title(stored, title_of(L("Lay out: %1%"), joined_titles(parts)));
    }

    if (definition->handler == ToolHandler::ObjectPlace) {
        const auto arguments = json::parse(stored.arguments_json);
        const ActivityTitle name = object_title(arguments["objectId"].get<std::string>());
        std::vector<ActivityTitle> parts;
        const auto number = [](double value) {
            std::ostringstream out;
            out << value;
            return out.str();
        };
        // TRN %1% is a unit, inches or meters.
        if (arguments.contains("unitsFix")) parts.push_back(title_of(L("convert from %1%"), arguments["unitsFix"].get<std::string>()));
        if (arguments.contains("scale"))
            // TRN %1%, %2% and %3% are scale factors along x, y and z.
            parts.push_back(title_of(L("scale x%1% y%2% z%3%"), number(arguments["scale"][0]), number(arguments["scale"][1]),
                                     number(arguments["scale"][2])));
        if (arguments.contains("scaleTo"))
            // TRN %1% is a size in millimetres, %2% an axis: x, y or z.
            parts.push_back(title_of(L("scale to %1% mm in %2%"), number(arguments["scaleTo"]["sizeMm"]),
                                     arguments["scaleTo"]["axis"].get<std::string>()));
        // TRN %1% is an axis: x, y or z.
        if (arguments.contains("mirrorAxis")) parts.push_back(title_of(L("mirror in %1%"), arguments["mirrorAxis"].get<std::string>()));
        if (arguments.contains("faceDown")) parts.push_back(title_of(L("put a chosen face down")));
        if (arguments.contains("rotateDegrees"))
            // TRN %1%, %2% and %3% are angles about x, y and z.
            parts.push_back(title_of(L("rotate %1%/%2%/%3% degrees"), number(arguments["rotateDegrees"][0]),
                                     number(arguments["rotateDegrees"][1]), number(arguments["rotateDegrees"][2])));
        if (arguments.contains("autoOrient")) parts.push_back(title_of(L("auto-orient")));
        if (arguments.contains("position"))
            // TRN %1% and %2% are a position's x and y on the plate.
            parts.push_back(title_of(L("move to %1%, %2%"), number(arguments["position"][0]), number(arguments["position"][1])));
        if (arguments.contains("dropToBed")) parts.push_back(title_of(L("drop to the bed")));
        // TRN %1% is an object's name, %2% a list of what is done to it, such as "rotate 0/0/90 degrees, drop to the bed".
        set_title(stored, title_of(L("Place %1%: %2%"), name, joined_titles(parts)));
    }

    if (definition->handler == ToolHandler::PrinterSetup) {
        auto arguments = json::parse(stored.arguments_json);
        const Workspace::PrinterSetupRequest request = setup_request(arguments);
        std::vector<ActivityTitle>           parts;
        if (!request.empty()) {
            const auto preview = m_workspace.preview_printer_setup(request);
            if (!preview.valid) {
                fail(stored, preview.issues.front().code, preview.issues.front().message,
                     printer_setup_preview_result(preview, json::array(), snapshot).dump());
                return stored;
            }
            // Store the previewed outcome, compared again before anything is applied.
            arguments["previewedSetup"] = setup_outcome(preview);
            std::string setup = preview.resulting.preset;
            if (request.plate_type) setup += ", " + preview.resulting.plate_type;
            if (request.process_preset) setup += ", " + preview.process_preset;
            if (request.filament_presets)
                for (std::size_t slot = 0; slot < request.filament_presets->size(); ++slot)
                    setup += ", " + (*request.filament_presets)[slot];
            // TRN %1% is a list of presets: the printer, then its plate, process and filaments.
            parts.push_back(title_of(L("Set up %1%"), setup));
            for (const auto& substitution : preview.substitutions)
                parts.push_back(substitution.kind == "filament" ?
                                    // TRN %1% is the name of the filament preset that is replaced.
                                    title_of(L("replaces filament %1%"), substitution.from) :
                                substitution.kind == "process" ?
                                    // TRN %1% is the name of the process preset that is replaced.
                                    title_of(L("replaces process %1%"), substitution.from) :
                                    // TRN %1% is the build plate type that is replaced, such as Textured PEI Plate.
                                    title_of(L("replaces plate %1%"), substitution.from));
            for (const auto& edits : preview.unsaved_edits)
                // TRN %1% is how many edits, %2% a preset's name.
                parts.push_back(plural_title("discards %1% unsaved edit in %2%", "discards %1% unsaved edits in %2%", edits.count,
                                             edits.preset));
        } else {
            parts.push_back(title_of(L("Record printer facts")));
        }
        if (arguments.contains("confirmFacts")) {
            if (m_product_state == nullptr)
                throw std::logic_error("printer_setup confirmFacts reached a coordinator with no product state");
            if (!m_product_state->has_printer_facts()) {
                fail(stored, "refused", "Printer facts cannot be recorded here: this project has no printer facts store.");
                return stored;
            }
            // A fact's name and value are the model's own words.
            for (const auto& fact : arguments["confirmFacts"])
                parts.push_back(same_title(fact["fact"].get<std::string>() + ": " + fact["value"].get<std::string>()));
        }
        stored.arguments_json = arguments.dump();
        set_title(stored, joined_titles(parts, "; "));
    }

    if (definition->handler == ToolHandler::ProjectOpen) {
        const auto arguments = json::parse(stored.arguments_json);
        if (arguments.contains("path")) {
            // Validate the name before the file is read during execution.
            const std::filesystem::path path = std::filesystem::u8path(arguments["path"].get<std::string>());
            std::error_code error;
            if (!path.is_absolute() || !std::filesystem::is_regular_file(path, error)) {
                fail(stored, "invalid_arguments", "Give the absolute path of a file that exists.");
                return stored;
            }
            if (path.extension() == ".3mf" && arguments.value("unitConversion", "keep") == "inches") {
                fail(stored, "invalid_arguments", "A project keeps its own units; inches applies to model files.");
                return stored;
            }
            // A file opened before: whether to continue its project is the
            // person's to say, through the caller.
            EarlierLookup earlier;
            try {
                earlier = m_find_earlier ? m_find_earlier(path.u8string()) : std::nullopt;
            } catch (const Workspace::ProjectsUnreadable& error) {
                fail(stored, "local_save_failed", std::string(kProjectsUnreadable) + " " + error.what());
                return stored;
            }
            if (earlier && !arguments.contains("openedBefore")) {
                const std::string changed = !earlier->changed ? "" : *earlier->changed ? " The file has changed since." :
                                                                                         " The file has not changed since.";
                const std::string details = json{{"project", earlier->name}, {"current", earlier->current},
                                                 {"fileChanged", earlier->changed ? json(*earlier->changed) : json()}}.dump();
                fail(stored, "opened_before",
                     (earlier->current ? "The open project, \"" + earlier->name + "\", was made from this file." :
                                         "\"" + earlier->name + "\" was made from this file before.") + changed +
                         " Ask the person whether to continue that project or start a new one from the file, then call again "
                         "with openedBefore \"existing\" or \"new\".",
                     details);
                return stored;
            }
            set_title(stored, earlier && arguments["openedBefore"] == "existing" ?
                                  // TRN %1% is a project's name.
                                  title_of(L("Continue %1%"), earlier->name) :
                                  // TRN %1% is a file's path.
                                  title_of(L("Open %1%"), arguments["path"].get<std::string>()));
        } else {
            set_title(stored, title_of(L("Start a new project")));
        }
    }

    if (definition->handler == ToolHandler::CalibrationGenerate) {
        // A range the dialog would refuse is refused here, before the open
        // project is closed for it.
        const Workspace::CalibrationRequest request = calibration_request(json::parse(stored.arguments_json));
        if (const std::string problem = Workspace::calibration_request_problem(request); !problem.empty()) {
            fail(stored, "invalid_arguments", problem);
            return stored;
        }
        set_title(stored, calibration_title(Workspace::with_dialog_defaults(request)));
    }

    if (definition->handler == ToolHandler::SettingsApplyPatch) {
        auto arguments = json::parse(stored.arguments_json);
        // In the project chat a preset is saved by the person, from the card
        // the Agent's preview drew.
        if (m_saves_need_approval && stored.source == ToolSource::Agent && stored.approves.empty() &&
            arguments.contains("persistAs")) {
            fail(stored, "refused",
                 "Saving a preset is the person's: your settings_preview_patch with persistAs shows them a card with Save "
                 "preset, and the app saves it when they press it. Do not apply it yourself; say what you propose and wait.");
            return stored;
        }
        if (!settings_revision_holds(arguments, snapshot, m_last_settings_revision)) {
            fail(stored, "stale_workspace", "The workspace changed. Read and preview again.", stale_settings_details(arguments, snapshot).dump());
            return stored;
        }
        const auto target = settings_target(arguments, snapshot);
        if (!target.found) {
            fail(stored, "not_found", kMissingTarget);
            return stored;
        }
        const auto patch   = settings_patch(arguments, target.target);
        const auto preview = m_workspace.preview_settings(patch);
        if (!preview.valid) {
            const auto& issue = preview.issues.at(0);
            fail(stored, issue.code, issue.message, settings_preview_result(preview, target.target.scope, snapshot).dump());
            return stored;
        }
        set_title(stored, settings_title(preview, target, patch));
        arguments["previewedChanges"] = json::array();
        for (const auto& change : Workspace::previewed_changes(preview))
            arguments["previewedChanges"].push_back({{"key", change.key}, {"before", change.before}, {"after", change.after}});
        stored.arguments_json = arguments.dump();
    }

    // A surface's own tool is checked by that surface before it runs: what
    // is valid there depends on what the surface is about, which the registry
    // cannot know.
    if (m_extension_preflight) {
        if (std::optional<ToolError> problem = m_extension_preflight(definition->handler, stored)) {
            fail(stored, problem->code, problem->message, problem->details_json);
            return stored;
        }
    }

    start_running(stored);
    return stored;
}

bool ToolExecutionCoordinator::approve_proposal(const std::string& preview_action_id)
{
    const ToolActivity* preview = find(preview_action_id);
    if (preview == nullptr || preview->proposal_json.empty() || preview->state != ToolState::Succeeded)
        throw std::logic_error("Save preset on " + preview_action_id + ", which is no open proposal");
    const bool saving = std::any_of(m_activities.begin(), m_activities.end(), [&preview_action_id](const ToolActivity& item) {
        return item.approves == preview_action_id && item.state != ToolState::Failed && item.state != ToolState::Cancelled;
    });
    if (saving)
        return false;
    // The preview's own stamp: a settings change since it makes the save
    // stale instead of saving something the card did not show.
    json arguments = json::parse(preview->arguments_json);
    arguments["expectedSessionId"] = std::to_string(preview->session);
    arguments["expectedRevision"]  = preview->expected_revision;
    const std::string correlation = preview->correlation_id;
    m_approving = preview_action_id;
    submit(ToolRequest{"settings_apply_patch", arguments.dump()}, correlation);
    m_approving.clear();
    return true;
}

bool ToolExecutionCoordinator::cancel(const std::string& action_id)
{
    ToolActivity* activity = find_mutable(action_id);
    if (activity == nullptr)
        return false;
    // Cancellation is possible while running before the final execution tick.
    // Terminal records never change.
    if (activity->state != ToolState::Running)
        return false;
    m_pending.erase(action_id);
    activity->state = ToolState::Cancelled;
    notify(*activity);
    return true;
}

bool ToolExecutionCoordinator::finish_pending(const std::string& action_id, ExtensionResult outcome)
{
    const auto waiting = m_pending.find(action_id);
    if (waiting == m_pending.end())
        return false;
    Pending pending = std::move(waiting->second);
    m_pending.erase(waiting);
    ToolActivity* activity = find_mutable(action_id);
    if (activity == nullptr)
        return false;
    if (outcome.pending || !outcome.handled)
        throw std::logic_error("A pending call to " + activity->tool + " was finished without an outcome");
    if (pending.dialogs)
        for (Workspace::OrcaMessage& answered : pending.dialogs->finish())
            pending.answered.push_back(std::move(answered));
    if (!pending.answered.empty())
        activity->app_dialogs_json = app_dialogs_result(pending.answered).dump();
    if (outcome.error) {
        fail(*activity, std::move(outcome.error->code), std::move(outcome.error->message));
        return true;
    }
    activity->result_json = std::move(outcome.result_json);
    activity->state       = ToolState::Succeeded;
    notify(*activity);
    return true;
}

bool ToolExecutionCoordinator::retitle_pending(const std::string& action_id, const ActivityTitle& title)
{
    ToolActivity* activity = m_pending.count(action_id) != 0 ? find_mutable(action_id) : nullptr;
    if (activity == nullptr)
        return false;
    set_title(*activity, title);
    notify(*activity);
    return true;
}

bool ToolExecutionCoordinator::answer_dialogs_while_pending(const std::string& action_id)
{
    const auto waiting = m_pending.find(action_id);
    if (waiting == m_pending.end())
        return false;
    if (!waiting->second.dialogs)
        waiting->second.dialogs = std::make_unique<Workspace::IWorkspace::ToolCallDialogs>(m_workspace);
    return true;
}

void ToolExecutionCoordinator::clear()
{
    for (const ToolActivity& activity : m_activities)
        if (activity.action_id != m_executing)
            m_pending.erase(activity.action_id);
    m_activities.erase(std::remove_if(m_activities.begin(), m_activities.end(),
                                      [this](const ToolActivity& activity) { return activity.action_id != m_executing; }),
                       m_activities.end());
}

void ToolExecutionCoordinator::forget_if_closed(const std::string& action_id)
{
    // The record belongs to the project that was closed. Its subscribers have
    // had the terminal result; the new project's action ids start again, and
    // must not meet it. A call pump() is running is announced only once it is
    // done, so its record waits for that.
    if (!m_announce_after.empty() && action_id == m_announce_after) {
        m_forget_after_announce = action_id;
        return;
    }
    const std::uint64_t session = m_workspace.snapshot().session.value();
    m_activities.erase(std::remove_if(m_activities.begin(), m_activities.end(),
                                      [&](const ToolActivity& activity) {
                                          return activity.action_id == action_id && activity.session != session;
                                      }),
                       m_activities.end());
}

namespace {

// Whether every plate a slice_start named (all of them when it named none)
// holds a current slice, and why the first that does not was refused or failed.
bool targets_sliced(const Workspace::WorkspaceSnapshot& snapshot, const std::optional<Workspace::PlateId>& plate,
                    std::optional<Workspace::SliceFailure>& failure)
{
    bool sliced = !snapshot.plates.empty();
    for (const auto& candidate : snapshot.plates) {
        if (plate && candidate.id != *plate)
            continue;
        if (!candidate.sliced) {
            sliced = false;
            if (!failure && candidate.slice_failure)
                failure = candidate.slice_failure;
        }
    }
    return sliced;
}

} // namespace

void ToolExecutionCoordinator::pump()
{
    for (ToolActivity& activity : m_activities) {
        // A pending call has run; its Job finishes it.
        if (activity.state != ToolState::Running || m_pending.count(activity.action_id) != 0) {
            continue;
        }
        if (activity.progress_current + 1 < activity.progress_total) {
            ++activity.progress_current;
            notify(activity);
        } else {
            activity.progress_current = activity.progress_total;
            const ToolDefinition* definition = m_registry.find(activity.tool);
            const bool versioned = m_version_before && m_version_after && definition != nullptr &&
                                   changes_printable_project(*definition, activity);
            std::string before;
            if (versioned) {
                try {
                    before = m_version_before(activity);
                } catch (const std::exception& error) {
                    std::clog << "Could not save before Agent action: " << error.what() << '\n';
                    fail(activity, "local_save_failed", std::string("Nothing ran: the project could not be saved first: ") + error.what() + ". Tell the person the project is not saved.");
                    return;
                }
            }
            // A tool that replaces the project clears the other activities
            // while it runs, which may move this one: read it before.
            const bool document_boundary = !versioned && m_document_boundary && activity.action_class != ActionClass::ReadOnly &&
                                           definition != nullptr && definition->handler != ToolHandler::ProjectOpen &&
                                           definition->handler != ToolHandler::CalibrationGenerate &&
                                           definition->handler != ToolHandler::SliceStart;
            // The save after a change is part of its outcome. Subscribers,
            // the model's tool result among them, are told that outcome once,
            // after the save, so a failed save is what they hear and the
            // record keeps what they heard.
            struct Saved { std::string& id; ~Saved() { id.clear(); } } saved{m_saving_after};
            if (versioned || document_boundary)
                m_saving_after = activity.action_id;
            // The app's dialogs that open while the call runs, its save
            // included, are answered and become part of its outcome, so
            // that outcome too is announced only once the call is done.
            const std::string ran = activity.action_id;
            struct Announced { std::string& id; ~Announced() { id.clear(); } } announced{m_announce_after};
            m_announce_after = ran;
            auto dialogs = std::make_unique<Workspace::IWorkspace::ToolCallDialogs>(m_workspace);
            execute(activity);
            std::optional<std::string> save_error;
            if (versioned) {
                try {
                    m_version_after(activity, before);
                } catch (const std::exception& error) {
                    std::clog << "Could not save after Agent action: " << error.what() << '\n';
                    save_error = error.what();
                }
            } else if (document_boundary) {
                try {
                    m_document_boundary(activity);
                } catch (const std::exception& error) {
                    std::clog << "Could not save Agent document: " << error.what() << '\n';
                    save_error = error.what();
                }
            }
            if (save_error && activity.state == ToolState::Failed)
                fail(activity, activity.error->code, activity.error->message + " The project also could not be saved: " + *save_error + ".", activity.error->details_json);
            else if (save_error)
                fail(activity, "local_save_failed", "The change was made, but the project could not be saved: " + *save_error + ". Do not repeat the call; tell the person the project is not saved.");
            m_saving_after.clear();
            // A call that replaced the project may have moved its record.
            ToolActivity* done = find_mutable(ran);
            // A pending call's dialogs are listed when it finishes.
            if (const auto waiting = m_pending.find(ran); waiting != m_pending.end()) {
                waiting->second.answered = dialogs->finish();
                return;
            }
            const std::vector<Workspace::OrcaMessage> answered = dialogs->finish();
            if (done != nullptr && !answered.empty())
                done->app_dialogs_json = app_dialogs_result(answered).dump();
            m_announce_after.clear();
            if (done != nullptr && tool_state_terminal(done->state))
                notify(*done);
            if (!m_forget_after_announce.empty())
                forget_if_closed(std::exchange(m_forget_after_announce, {}));
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
    activity.state = ToolState::Running;
    notify(activity);
}

void ToolExecutionCoordinator::execute(ToolActivity& activity)
{
    // Recheck the last content/history change here; selection-only changes
    // still cannot redirect an action pinned to an object ID.
    const std::uint64_t invalidated = settings_patch_tool(activity.tool) ? m_last_settings_revision : m_last_invalidating_revision;
    const bool          changed     = invalidated > activity.expected_revision;
    if (activity.action_class != ActionClass::ReadOnly &&
        (m_workspace.snapshot().session.value() != activity.session || changed)) {
        fail(activity, "stale_workspace", "Nothing ran: the project changed after this call was made. Read the workspace again (for a settings change, preview again), then call again.");
        return;
    }
    // Whatever this action changes is its caller's, in the change log.
    const Workspace::IWorkspace::AgentEdit agent_edit(
        m_workspace, activity.source == ToolSource::Mcp ? Workspace::EditActor::ExternalTool : Workspace::EditActor::Agent,
        activity.client);
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
            sections = {asked("summary"), asked("plan"), asked("slicing"), asked("printer"), asked("project"), asked("objects"), asked("activities")};
        }
        if ((sections.plan || sections.printer) && m_product_state == nullptr)
            throw std::logic_error("workspace_inspect reached a coordinator with no product state");
        json result = workspace_inspection(m_workspace.snapshot(), sections);
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
        if (sections.activities) {
            // The latest calls of either adapter, oldest first.
            constexpr std::size_t kRecentActivities = 20;
            result["activities"] = json::array();
            const std::size_t first = m_activities.size() > kRecentActivities + 1 ? m_activities.size() - kRecentActivities - 1 : 0;
            for (std::size_t index = first; index < m_activities.size(); ++index) {
                const ToolActivity& recent = m_activities[index];
                if (recent.action_id == activity.action_id)
                    continue;
                json row{{"actionId", recent.action_id}, {"tool", recent.tool}, {"title", recent.title}, {"state", tool_state_name(recent.state)}};
                if (recent.error) row["error"] = json{{"code", recent.error->code}, {"message", recent.error->message}};
                result["activities"].push_back(std::move(row));
            }
        }
        activity.result_json = result.dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::ProjectOpen) {
        const auto arguments = json::parse(activity.arguments_json);
        Workspace::ProjectOpenRequest request;
        request.path                  = arguments.value("path", "");
        request.new_project           = arguments.value("new", false);
        request.load_project_settings = arguments.value("loadProjectSettings", "project") == "project";
        request.units                 = arguments.value("unitConversion", "keep") == "inches" ? Workspace::UnitChoice::Inches :
                                                                                               Workspace::UnitChoice::Keep;
        // The conversation belongs to the project being closed; it goes to
        // its recovery state before the project does.
        if (m_product_state != nullptr)
            m_product_state->flush_to_project();
        Workspace::LoadReport report;
        const std::string action_id = activity.action_id;
        EarlierLookup earlier;
        try {
            if (arguments.value("openedBefore", "") == "existing" && m_find_earlier)
                earlier = m_find_earlier(request.path);
        } catch (const Workspace::ProjectsUnreadable& error) {
            fail(activity, "local_save_failed", std::string(kProjectsUnreadable) + " " + error.what());
            forget_if_closed(action_id);
            return;
        }
        auto opened = Workspace::CommandResult::success();
        if (earlier) {
            report.project_opened = true;
            const std::string problem = m_open_earlier ? m_open_earlier(earlier->id) : "nothing here opens saved projects";
            if (!problem.empty())
                opened = Workspace::CommandResult::failure(Workspace::WorkspaceError::Failed,
                                                           "The saved project could not be opened: " + problem);
        } else {
            opened = m_workspace.open_project(request, report);
            if (opened.succeeded() && !request.new_project && m_opened_from)
                m_opened_from(request.path);
        }
        // Replacing the project clears the other activities, which may move
        // this one: find it again rather than trust the reference.
        ToolActivity& current = *find_mutable(action_id);
        if (!opened.succeeded()) {
            fail(current, workspace_error_code(opened.error), opened.message, json{{"report", load_report_result(report)}}.dump());
            forget_if_closed(action_id);
            return;
        }
        const auto snapshot = m_workspace.snapshot();
        std::size_t objects = 0;
        for (const auto& plate : snapshot.plates) objects += plate.objects.size();
        current.result_json = json{{"projectName", snapshot.setup.project_name}, {"path", snapshot.setup.project_path},
                                    {"plateCount", snapshot.plates.size()}, {"objectCount", objects},
                                    {"report", load_report_result(report)},
                                    {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}}
                                   .dump();
        current.state = ToolState::Succeeded;
        const bool by_agent = current.source == ToolSource::Agent;
        notify(current);
        forget_if_closed(action_id);
        if (by_agent && m_agent_project_opened)
            m_agent_project_opened(report);
        return;
    }

    if (definition->handler == ToolHandler::CalibrationGenerate) {
        // Read first: opening a calibration project clears the other
        // activities and may move this one, so `activity` is not used after.
        const json                          arguments = json::parse(activity.arguments_json);
        const Workspace::CalibrationRequest request   = Workspace::with_dialog_defaults(calibration_request(arguments));
        const std::string                   action_id = activity.action_id;
        const bool                          by_agent  = activity.source == ToolSource::Agent;
        // One rule, whichever path reached the tool: the test is made in a
        // calibration project. From any other project (the symptom path) that
        // project is released as an open releases it, and the calibration
        // project for its filament and printer opened through the same path.
        const bool in_calibration_project = m_calibration.is_open && m_calibration.is_open();
        if (!in_calibration_project) {
            const auto          snapshot = m_workspace.snapshot();
            CalibrationHandover handover{snapshot.setup.project_name,
                                         preset_label(m_workspace, Workspace::PresetKind::Filament, snapshot.setup.filament_preset),
                                         preset_label(m_workspace, Workspace::PresetKind::Printer, snapshot.setup.printer_preset),
                                         arguments.value("symptom", "")};
            // The conversation belongs to the project being closed.
            if (m_product_state != nullptr)
                m_product_state->flush_to_project();
            Workspace::ProjectOpenRequest open;
            open.new_project = true;
            open.name        = calibration_project_name(handover.filament, handover.printer);
            Workspace::LoadReport report;
            const auto opened = m_workspace.open_project(open, report);
            if (!opened.succeeded()) {
                fail(*find_mutable(action_id), workspace_error_code(opened.error), opened.message);
                forget_if_closed(action_id);
                return;
            }
            if (m_calibration.opened)
                m_calibration.opened(handover, by_agent);
        }
        Workspace::CalibrationResult made;
        auto generated = Workspace::CommandResult::success();
        const auto generate = [&] { generated = m_workspace.generate_calibration(request, made); };
        if (m_calibration.continuing)
            m_calibration.continuing(generate);
        else
            generate();
        ToolActivity& current = *find_mutable(action_id);
        // The generator starts a new workspace session. A test made in the
        // calibration project that asked continues that project, so the call
        // takes the session it made: AgentHost records a call only in the
        // session it belongs to, and this one's result belongs to the chat
        // that asked. From another project the call stays with the project it
        // closed, as an open's does, and that chat keeps the last word.
        if (in_calibration_project)
            current.session = m_workspace.snapshot().session.value();
        if (!generated.succeeded()) {
            fail(current, workspace_error_code(generated.error), generated.message);
            forget_if_closed(action_id);
            if (m_calibration.finished)
                m_calibration.finished(false, by_agent);
            return;
        }
        json objects = json::array();
        for (std::size_t index = 0; index < made.objects.size() && index < 64; ++index)
            objects.push_back({{"objectId", std::to_string(made.objects[index].value())}, {"name", made.object_names[index]}});
        json applied = json::object();
        if (made.start) applied["start"] = *made.start;
        if (made.end) applied["end"] = *made.end;
        if (made.step) applied["step"] = *made.step;
        if (request.test == Workspace::CalibrationTest::PressureAdvance) {
            applied["method"]       = pa_method_name(request.method);
            applied["extruderType"] = request.bowden ? "bowden" : "direct_drive";
        }
        if (request.test == Workspace::CalibrationTest::Flow) {
            applied["pass"]   = request.flow_pass;
            applied["linear"] = request.flow_linear;
        }
        Workspace::LoadReport said;
        said.loads.push_back({{}, false, made.messages});
        const std::string summary = calibration_summary(request, made);
        const auto snapshot = m_workspace.snapshot();
        current.result_json = json{{"projectName", made.project_name}, {"test", arguments["test"]},
                                   {"plateId", std::to_string(made.plate.value())}, {"objects", std::move(objects)},
                                   {"applied", std::move(applied)}, {"messages", orca_messages_result(said)},
                                   {"summary", summary}, {"sessionId", std::to_string(snapshot.session.value())},
                                   {"revision", snapshot.revision}}
                                  .dump();
        current.state = ToolState::Succeeded;
        notify(current);
        forget_if_closed(action_id);
        if (m_calibration.finished)
            m_calibration.finished(true, by_agent);
        return;
    }

    if (definition->handler == ToolHandler::ObjectImport || definition->handler == ToolHandler::ObjectImportFile) {
        const auto arguments = json::parse(activity.arguments_json);
        const Workspace::ProjectSessionId session(std::stoull(arguments["sessionId"].get<std::string>()));
        Workspace::ImportRequest request;
        if (definition->handler == ToolHandler::ObjectImport) {
            const std::string attachment = arguments["attachmentId"].get<std::string>();
            request.path = m_attachment_path_resolver ? m_attachment_path_resolver(attachment) : std::string();
            // The tool is offered whether or not anything is attached, so the
            // tool list stays the same for the whole chat.
            if (request.path.empty()) {
                fail(activity, "invalid_arguments",
                     "No model file attached in this chat has the id \"" + attachment +
                         "\". Nothing was imported. Use an attachment id from the person's message; if they attached "
                         "no model file, ask them to attach it.");
                return;
            }
        } else {
            request.path = arguments["path"].get<std::string>();
        }
        if (arguments.contains("plateId"))
            request.plate = Workspace::PlateId(session, std::stoull(arguments["plateId"].get<std::string>()));
        request.units = arguments.value("unitConversion", "keep") == "inches" ? Workspace::UnitChoice::Inches :
                                                                               Workspace::UnitChoice::Keep;
        if (session != m_workspace.snapshot().session) {
            fail(activity, "stale_workspace", "That project is no longer open. Read workspace_inspect again.");
            return;
        }
        Workspace::LoadReport            report;
        std::vector<Workspace::ObjectId> added;
        const auto imported = m_workspace.import_objects(request, report, added);
        if (!imported.succeeded()) {
            fail(activity, workspace_error_code(imported.error), imported.message, json{{"report", load_report_result(report)}}.dump());
            return;
        }
        json ids = json::array();
        for (const auto& id : added)
            if (ids.size() < 64) ids.push_back(std::to_string(id.value()));
        // The new objects as they are now, sizes included.
        std::vector<Workspace::ObjectDetails> rows;
        for (const auto& row : m_workspace.object_details())
            if (std::find(added.begin(), added.end(), row.id) != added.end()) rows.push_back(row);
        const auto snapshot  = m_workspace.snapshot();
        activity.result_json = json{{"objectIds", std::move(ids)}, {"objects", objects_section_result(rows)},
                                    {"report", load_report_result(report)},
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
            if (m_product_state == nullptr)
                throw std::logic_error("project_delete_items reached a coordinator with no product state");
            for (const Workspace::RegionRecord& record : m_product_state->regions())
                (removed.count(record.id) ? regions : kept).push_back(record);
            if (regions.size() != removed.size()) {
                fail(activity, "not_found", "A named region is not in the project; read object_analyze's regions section.");
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
                                   throw std::logic_error("A checked objectId did not parse");
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
        // OrcaSlicer keeps at least one plate in every project.
        if (!plate)
            throw std::logic_error("slice_inspect found no active plate");
        if (std::none_of(snapshot.plates.begin(), snapshot.plates.end(),
                         [&plate](const Workspace::WorkspacePlate& candidate) { return candidate.id == *plate; })) {
            fail(activity, "not_found", kPlateNotFound);
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
        std::string kind, message;
        bool        cancelled = false;
        if (handle == m_slice_handle && !handle.empty()) {
            kind = "slice";
            if (before.slicing.running && m_slice_running)
                m_workspace.cancel_slice(cancelled);
            message = cancelled ? "The slice was stopped." :
                                  "Nothing was stopped. The tools stop only a slice they started.";
        } else if (std::any_of(before.jobs.begin(), before.jobs.end(), [&handle](const auto& job) { return job.handle == handle; })) {
            kind = "job";
            const auto done = m_workspace.cancel_job(handle, cancelled);
            if (!done.succeeded()) {
                fail(activity, workspace_error_code(done.error), done.message);
                return;
            }
            message = cancelled ? "Cancelling the job; its end shows in workspace_inspect's slicing section." : "That job had already ended.";
        } else {
            fail(activity, "not_found", "Nothing the tools started has the handle " + handle + ".");
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
                                        throw std::logic_error("A checked objectId did not parse");
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
                                   throw std::logic_error("A checked objectId did not parse");
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
                                   throw std::logic_error("A checked objectId did not parse");
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

    if (definition->handler == ToolHandler::ObjectAddShape) {
        const auto arguments    = json::parse(activity.arguments_json);
        const auto object       = parse_object_argument(activity.arguments_json);
        const auto bound_before = bound_regions();
        std::vector<Workspace::AddedPart> added;
        const auto done = object ? m_workspace.add_shapes(*object, shape_requests(arguments), added) :
                                   throw std::logic_error("A checked objectId did not parse");
        if (!done.succeeded()) {
            fail(activity, workspace_error_code(done.error), done.message);
            return;
        }
        json parts = json::array();
        for (const Workspace::AddedPart& part : added)
            parts.push_back({{"partId", std::to_string(part.id)}, {"type", part.type}, {"label", part.label.substr(0, kToolTextLimit)}});
        const auto snapshot  = m_workspace.snapshot();
        activity.result_json = json{{"objectId", arguments["objectId"]}, {"parts", std::move(parts)},
                                    {"regionsUnbound", regions_unbound(bound_before)}, {"projectUndo", true},
                                    {"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision}}
                                   .dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::ObjectMarkRegion) {
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
        if (regions && m_product_state == nullptr)
            throw std::logic_error("object_analyze reached a coordinator with no product state");
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
                                       throw std::logic_error("A checked objectId did not parse");
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
            if (setup_outcome(m_workspace.preview_printer_setup(request)) != arguments.at("previewedSetup")) {
                fail(activity, "stale_workspace", "The presets changed since this setup was previewed. Preview it again.");
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
        // OrcaSlicer keeps at least one plate in every project.
        if (!plate)
            throw std::logic_error("slice_report found no active plate");
        if (std::none_of(snapshot.plates.begin(), snapshot.plates.end(),
                         [&plate](const Workspace::WorkspacePlate& candidate) { return candidate.id == *plate; })) {
            fail(activity, "not_found", kPlateNotFound);
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
                        asked("seams"),   asked("firstLayer"), asked("islands")};
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
        activity.result_json = result.dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::PrintDialogOpen) {
        const auto arguments = json::parse(activity.arguments_json);
        PrintDialogRequest request;
        if (arguments.contains("plateId"))
            request.plate = Workspace::PlateId(Workspace::ProjectSessionId(activity.session),
                                               std::stoull(arguments["plateId"].get<std::string>()));
        request.gcode = arguments.value("gcode", std::string());
        if (!m_print_dialog_opener)
            throw std::logic_error("print_dialog_open is registered with no host to open the dialog");
        // A plate's slice is what Print sends: none, or a stale one, is a
        // slice to run first. A recorded G-code needs no slice.
        if (request.gcode.empty()) {
            const auto before = m_workspace.snapshot();
            if (!request.plate)
                request.plate = before.active_plate;
            if (!request.plate) {
                fail(activity, "workspace_unavailable", "The project has no plate to print.");
                return;
            }
            std::optional<Workspace::SliceFailure> failure;
            if (!targets_sliced(before, request.plate, failure)) {
                fail(activity, "needs_step", "This plate has no valid slice. Slice it with slice_start first, then open the print dialog.");
                return;
            }
        }
        const PrintDialogOutcome outcome = m_print_dialog_opener(request);
        if (!outcome.error.empty()) {
            fail(activity, outcome.error, outcome.message);
            return;
        }
        // The dialog is modal: it opens after this call returns, and the
        // person sends from it, or not. Nothing about a send is known here.
        const auto snapshot = m_workspace.snapshot();
        activity.result_json = json{{"opened", true},
                                    {"dialog", outcome.dialog},
                                    {"note", "The app's send dialog is open. The person checks it and presses Send there; "
                                             "nothing is sent until they do. A send that happens shows in the print history."},
                                    {"sessionId", std::to_string(snapshot.session.value())},
                                    {"revision", snapshot.revision}}
                                   .dump();
        activity.state = ToolState::Succeeded;
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
            fail(activity, workspace_error_code(started.error), started.message);
            return;
        }
        // OrcaSlicer decides inside that call: a run began, nothing needed
        // slicing, or its check refused the plate and started nothing, with
        // the reason recorded on the plate before the call returned.
        const auto snapshot = m_workspace.snapshot();
        json result{{"handle", activity.action_id}, {"started", snapshot.slicing.running}};
        if (snapshot.slicing.running) {
            // The run is Orca's, and it outlives this call: the handle is how
            // the caller finds it again in the slicing section, which is where
            // the result appears when the slicer is done.
            m_slice_handle  = activity.action_id;
            m_slice_running = true;
            m_slice_session = snapshot.session;
            result["note"]  = activity.source == ToolSource::Agent ?
                "Slicing has started in the background, and its outcome is not known yet. The person sees its progress "
                "in a card in the chat, below this turn, with a Cancel button. Until the run ends, slicing.running "
                "stays true and slice_report returns valid: false. The outcome comes to you without a call: while this "
                "turn goes on, in the workspace of your next tool result; after it, in a new turn the app starts when "
                "the run ends." :
                "Slicing has started in the background. Until the run ends, slicing.running stays true and slice_report "
                "returns valid: false. Nothing notifies this client when it ends: the outcome shows in workspace_inspect's "
                "slicing section, and slice_report reads the finished slice.";
        } else {
            std::optional<Workspace::SliceFailure> failure;
            if (targets_sliced(snapshot, plate, failure)) {
                result["alreadySliced"] = true;
            } else {
                if (!failure) {
                    failure.emplace();
                    failure->severity = "blocker";
                    failure->message  = "The app did not start slicing this plate.";
                }
                result["reason"] = slice_failure_result(*failure);
            }
        }
        result["slicing"]    = slicing_section_result(snapshot, m_slice_handle);
        result["sessionId"]  = std::to_string(snapshot.session.value());
        result["revision"]   = snapshot.revision;
        activity.result_json = result.dump();
        activity.state       = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::SetupRecord) {
        if (m_product_state == nullptr)
            throw std::logic_error("setup_record reached a coordinator with no product state");
        const auto arguments = json::parse(activity.arguments_json);
        const auto snapshot  = m_workspace.snapshot();
        // The purpose is the chat's title; what this call did to it is stated
        // in the reply, beside the setup in force now, which was chosen before
        // a first or changed purpose.
        const auto purpose = [this, &activity]() {
            json stated = json::object();
            if (const auto title = m_product_state->setup_title(activity.correlation_id); title && !title->empty())
                stated["purpose"] = *title;
            return stated;
        };
        // The records are a chat's own; a call no chat holds has none.
        if (!m_product_state->setup_title(activity.correlation_id)) {
            fail(activity, "refused", "Only a chat's own call records its purpose and plan.");
            return;
        }
        const json before = purpose();
        // The purpose goes first: one the chat refuses records nothing at all.
        // Left out, the recorded purpose stays, whether or not there is one.
        if (arguments.contains("purpose") &&
            !m_product_state->set_setup_title(activity.correlation_id, arguments.at("purpose").get<std::string>())) {
            fail(activity, "invalid_arguments", "purpose must be one line of up to 120 characters.");
            return;
        }
        // Neither record is in Orca's undo stack: say so where every other
        // mutation says it.
        json result{{"sessionId", std::to_string(snapshot.session.value())}, {"revision", snapshot.revision},
                    {"projectUndo", false}};
        const json after = purpose();
        // A first or changed purpose asks for the setup to be checked against
        // it; a call that restates or leaves out the purpose to change the
        // plan does not.
        const bool first   = before.empty() && !after.empty();
        const bool changed = !before.empty() && before != after;
        result["purposeChange"] = first ? "first" : changed ? "changed" : "unchanged";
        if (changed)
            result["purposeBefore"] = before;
        if (first || changed) {
            result["purposeAfter"] = after;
            m_product_state->set_purpose_setup(setup_in_force(snapshot));
        }
        result["setupInForce"] = setup_in_force(snapshot);
        if (arguments.contains("headline")) {
            const json& stated = arguments;
            PlanRecord plan;
            plan.headline = stated.at("headline").get<std::string>();
            for (const auto& decision : stated.value("decisions", json::array()))
                plan.decisions.push_back({decision.at("topic").get<std::string>(),
                                          decision.at("statement").get<std::string>(),
                                          decision.value("confidence", ""), decision.value("alternative", "")});
            for (const auto& line : stated.value("unverified", json::array()))
                plan.assumptions.push_back(line.get<std::string>());
            for (const auto& line : stated.value("risks", json::array()))
                plan.risks.push_back(line.get<std::string>());
            result["plan"] = plan_section_result(m_product_state->set_plan(std::move(plan)));
        } else {
            result["plan"] = plan_section_result(m_product_state->plan());
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
        const auto target   = settings_target(args, m_workspace.snapshot());
        if (!target.found) {
            fail(activity, "not_found", kMissingTarget);
            return;
        }
        query.target      = target.target;
        const auto result = m_workspace.search_settings(query);
        if (result.error) {
            fail(activity, result.error->code, result.error->message, setting_issue_result(*result.error).dump());
            return;
        }
        activity.result_json = settings_search_result(result, query.target.scope, m_workspace.snapshot()).dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::SettingsGet) {
        const auto args = json::parse(activity.arguments_json);
        const auto target = settings_target(args, m_workspace.snapshot());
        if (!target.found) {
            fail(activity, "not_found", kMissingTarget);
            return;
        }
        const auto result = m_workspace.read_settings(args.at("keys").get<std::vector<std::string>>(), target.target);
        if (result.error) {
            fail(activity, result.error->code, result.error->message, setting_issue_result(*result.error).dump());
            return;
        }
        activity.result_json = settings_read_result(result, target.target.scope, m_workspace.snapshot()).dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::SettingsPreviewPatch) {
        const auto args   = json::parse(activity.arguments_json);
        const auto target = settings_target(args, m_workspace.snapshot());
        if (!target.found) {
            fail(activity, "not_found", kMissingTarget);
            return;
        }
        const auto result = m_workspace.preview_settings(settings_patch(args, target.target));
        if (!result.issues.empty() && result.issues.front().code == "workspace_unavailable") {
            fail(activity, result.issues.front().code, result.issues.front().message);
            return;
        }
        const auto snapshot = m_workspace.snapshot();
        activity.result_json = settings_preview_result(result, target.target.scope, snapshot).dump();
        if (m_saves_need_approval && activity.source == ToolSource::Agent && result.valid && args.contains("persistAs")) {
            std::vector<std::string> keys;
            for (const auto& change : result.changes)
                keys.push_back(change.key);
            const Workspace::PresetKind kind = target.target.scope == Workspace::SettingsScope::Filament ? Workspace::PresetKind::Filament :
                                               target.target.scope == Workspace::SettingsScope::Printer  ? Workspace::PresetKind::Printer :
                                                                                                           Workspace::PresetKind::Process;
            const json proposal = settings_proposal(result, target.target, args["persistAs"].get<std::string>(),
                                                    m_workspace.read_settings(keys, target.target).items,
                                                    preset_label(m_workspace, kind, result.preset), snapshot);
            if (!proposal.is_null())
                activity.proposal_json = proposal.dump();
        }
        activity.state = ToolState::Succeeded;
        notify(activity);
        return;
    }

    if (definition->handler == ToolHandler::SettingsApplyPatch) {
        const auto args = json::parse(activity.arguments_json);
        const auto before = m_workspace.snapshot();
        if (!settings_revision_holds(args, before, m_last_settings_revision)) {
            fail(activity, "stale_workspace", "The workspace changed. Read and preview again.", stale_settings_details(args, before).dump());
            return;
        }
        std::vector<Workspace::SettingChange> confirmed;
        for (const auto& change : args.at("previewedChanges"))
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
            fail(activity, issue.code, issue.message, settings_preview_result(applied, target.target.scope, m_workspace.snapshot()).dump());
            return;
        }
        if (!result.succeeded() && result.error != WorkspaceError::NoChange) {
            fail(activity, workspace_error_code(result.error), result.message);
            return;
        }
        activity.result_json = settings_apply_result(applied, target.target.scope, m_workspace.snapshot(), result.succeeded()).dump();
        activity.state = ToolState::Succeeded;
        notify(activity);
        if (!activity.approves.empty() && !applied.saved_as.empty() && m_proposal_saved)
            m_proposal_saved(activity);
        return;
    }

    // The page's skills and the printer panel's own tools: each is owned by
    // the surface that asked for it, and each still runs here, inside this
    // execution state machine.
    if (definition->handler == ToolHandler::SkillRead || has_exposure(definition->exposure, ToolExposure::Printer)) {
        if (!m_extension_executor)
            throw std::logic_error(activity.tool + " is a surface's own tool, and no surface executes them here");
        ExtensionResult result = m_extension_executor(definition->handler, activity);
        if (result.handled) {
            if (result.pending) {
                m_pending.emplace(activity.action_id, Pending{});
                return;
            }
            if (result.error) {
                fail(activity, std::move(result.error->code), std::move(result.error->message));
                return;
            }
            activity.result_json = std::move(result.result_json);
            activity.state       = ToolState::Succeeded;
            notify(activity);
            return;
        }
        throw std::logic_error("The surface's executor did not handle " + activity.tool);
    }

    throw std::logic_error(activity.tool + " is registered with no handler");
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
    if (!ToolErrors::registered(code))
        throw std::logic_error("Tool error code \"" + code + "\" is not listed in ToolErrors.hpp");
    // A stale call names the project it expected and the project as it is,
    // whichever path found it stale.
    if (code == "stale_workspace" && details_json == "{}") {
        const json arguments = json::parse(activity.arguments_json, nullptr, false);
        const auto snapshot  = m_workspace.snapshot();
        std::string expected = std::to_string(activity.session);
        if (arguments.is_object() && arguments.contains("sessionId") && arguments["sessionId"].is_string())
            expected = arguments["sessionId"].get<std::string>();
        details_json = json{{"expectedSessionId", expected}, {"expectedRevision", activity.expected_revision},
                            {"currentSessionId", std::to_string(snapshot.session.value())}, {"currentRevision", snapshot.revision}}
                           .dump();
    }
    // Terminal records never change once announced; only a change whose save
    // is still to come has not been announced yet.
    if (tool_state_terminal(activity.state) && activity.action_id != m_saving_after)
        throw std::logic_error("Tool call " + activity.action_id + " already ended; its outcome cannot change");
    activity.state = ToolState::Failed;
    activity.error = ToolError{std::move(code), std::move(message), std::move(details_json)};
    notify(activity);
}

void ToolExecutionCoordinator::notify(const ToolActivity& activity)
{
    // pump() announces this outcome once the call and the save after it
    // are done.
    if (tool_state_terminal(activity.state) &&
        ((!m_saving_after.empty() && activity.action_id == m_saving_after) ||
         (!m_announce_after.empty() && activity.action_id == m_announce_after)))
        return;
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
    // Running actions recheck this revision at execution, without invalidating
    // a command on its own event.
    if ((change.reasons & kSettingsReasons) != WorkspaceChangeReasons::None)
        m_last_settings_revision = change.revision;
    // A new or lost slice leaves every other action as it was, but a queued
    // export of a sliced file was prepared from the previous slice.
    if (Workspace::has_reason(change.reasons, WorkspaceChangeReasons::Slicing))
        for (ToolActivity& activity : m_activities)
            if (activity.state == ToolState::Running && activity.action_id != m_executing && exports_slice(activity))
                fail(activity, "stale_workspace", "Nothing was written: the slice this export would write changed. Read the workspace again, then call again.");
    if ((change.reasons & kInvalidatingReasons) == WorkspaceChangeReasons::None)
        return;
    m_last_invalidating_revision = change.revision;
}

} // namespace Slic3r::GUI::JusPrin::Agent
