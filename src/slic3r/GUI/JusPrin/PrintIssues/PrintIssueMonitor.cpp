#include "PrintIssueMonitor.hpp"

#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/BackgroundSlicingProcess.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/JusPrin/Workspace/PlaterProjectState.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SliceBounds.hpp"

#include <boost/format.hpp>

#include <algorithm>
#include <chrono>
#include <set>

namespace Slic3r::GUI {
// OrcaSlicer's words for a copy over the plate's edge and for a filament a
// nozzle cannot reach, rebuilt by GLCanvas3D::check_volumes_outside_state.
// No header declares them; Plater.cpp and FilamentMapDialog.cpp reach them
// the same way.
std::string& get_object_clashed_text();
std::string& get_left_extruder_unprintable_text();
std::string& get_right_extruder_unprintable_text();
} // namespace Slic3r::GUI

namespace Slic3r::GUI::JusPrin::PrintIssues {

namespace {

// Where a native check points, by the ids the project model gives it. The
// print works on its own copy of the model, whose ids are the project's.
void set_target(PrintIssue& issue, const ModelObject* object, const ModelInstance* instance)
{
    if (instance != nullptr) {
        issue.instance = instance->id().id;
        object = instance->get_object();
    }
    if (object != nullptr) {
        issue.object      = object->id().id;
        issue.object_name = object->name;
    }
}

void set_target(PrintIssue& issue, const ObjectBase* target)
{
    if (target == nullptr)
        return;
    if (const auto* instance = dynamic_cast<const ModelInstance*>(target))
        set_target(issue, nullptr, instance);
    else if (const auto* print_object = dynamic_cast<const PrintObjectBase*>(target))
        set_target(issue, print_object->model_object(), nullptr);
    else if (const auto* object = dynamic_cast<const ModelObject*>(target))
        set_target(issue, object, nullptr);
}

std::string target_key(const PrintIssue& issue)
{
    return std::to_string(issue.object) + ":" + std::to_string(issue.instance);
}

// OrcaSlicer sets the check type on some results and leaves it unset on
// others, so only a value it defines is reported.
std::string check_name(StringExceptionType type)
{
    switch (type) {
    case STRING_EXCEPT_FILAMENT_NOT_MATCH_BED_TYPE:     return "filament_not_match_bed_type";
    case STRING_EXCEPT_FILAMENTS_DIFFERENT_TEMP:        return "filaments_different_temp";
    case STRING_EXCEPT_OBJECT_COLLISION_IN_SEQ_PRINT:   return "object_collision_in_seq_print";
    case STRING_EXCEPT_OBJECT_COLLISION_IN_LAYER_PRINT: return "object_collision_in_layer_print";
    case STRING_EXCEPT_LAYER_HEIGHT_EXCEEDS_LIMIT:      return "layer_height_exceeds_limit";
    default:                                            return {};
    }
}

PrintIssue plate_issue(PartPlate& plate, IssueSource source, IssueSeverity severity, std::string code, std::string message)
{
    PrintIssue issue;
    issue.source   = source;
    issue.severity = severity;
    issue.code     = std::move(code);
    issue.message  = std::move(message);
    issue.plate    = plate.id().id;
    issue.id       = std::string(source_name(source)) + ":" + issue.code;
    return issue;
}

// "1, 3" for filaments 0 and 2: OrcaSlicer counts them from one for people.
std::string filament_numbers(const std::set<int>& filaments)
{
    std::string text;
    for (const int filament : filaments)
        text += (text.empty() ? "" : ", ") + std::to_string(filament + 1);
    return text;
}

} // namespace

PrintIssueMonitor::PrintIssueMonitor(Plater& plater)
    : m_plater(plater), m_self(std::make_shared<PrintIssueMonitor*>(this))
{
    wxASSERT(wxIsMainThread());
    m_project_subscription = m_plater.subscribe_project_state([this](const ProjectStateChanged& change) {
        const auto has = [&change](ProjectStateChangeReason reason) {
            return (static_cast<std::uint32_t>(change.reasons) & static_cast<std::uint32_t>(reason)) != 0;
        };
        if (change.project_replaced) {
            m_failures.clear();
            m_validation.clear();
            m_result = {};
        }
        if (change.project_replaced || has(ProjectStateChangeReason::Objects) || has(ProjectStateChangeReason::Transform) ||
            has(ProjectStateChangeReason::Plates) || has(ProjectStateChangeReason::Project) ||
            has(ProjectStateChangeReason::Settings))
            ++m_project_changes;
        schedule();
    });
    // The four moments OrcaSlicer's own state can move without the project
    // changing: it re-evaluated whether the plate can be sliced or printed,
    // one of the Plater's timers fired (the deferred update of the print is
    // one of them; they cannot be told apart from outside, and a recompute
    // too many costs nothing), a slicing step reported a warning, and a slice
    // ended.
    m_plater.Bind(EVT_SLICE_STATUS_CHANGED, &PrintIssueMonitor::on_slice_status, this);
    m_plater.Bind(wxEVT_TIMER, &PrintIssueMonitor::on_timer, this);
    m_plater.Bind(EVT_SLICING_UPDATE, &PrintIssueMonitor::on_slicing_update, this);
    m_plater.Bind(EVT_PROCESS_COMPLETED, &PrintIssueMonitor::on_process_completed, this);
    schedule();
}

PrintIssueMonitor::~PrintIssueMonitor()
{
    wxASSERT(wxIsMainThread());
    m_plater.Unbind(EVT_SLICE_STATUS_CHANGED, &PrintIssueMonitor::on_slice_status, this);
    m_plater.Unbind(wxEVT_TIMER, &PrintIssueMonitor::on_timer, this);
    m_plater.Unbind(EVT_SLICING_UPDATE, &PrintIssueMonitor::on_slicing_update, this);
    m_plater.Unbind(EVT_PROCESS_COMPLETED, &PrintIssueMonitor::on_process_completed, this);
}

void PrintIssueMonitor::on_slice_status(wxCommandEvent& event)
{
    event.Skip();
    schedule();
}

void PrintIssueMonitor::on_timer(wxTimerEvent& event)
{
    // OrcaSlicer's own handler applies the edit when this returns; the
    // recompute is queued, so it reads the print after that.
    event.Skip();
    schedule();
}

void PrintIssueMonitor::on_slicing_update(SlicingStatusEvent& event)
{
    event.Skip();
    if (event.status.flags &
        (PrintBase::SlicingStatus::UPDATE_PRINT_STEP_WARNINGS | PrintBase::SlicingStatus::UPDATE_PRINT_OBJECT_STEP_WARNINGS))
        schedule();
}

void PrintIssueMonitor::on_process_completed(SlicingProcessCompletedEvent& event)
{
    event.Skip();
    if (event.error()) {
        PartPlate* plate = m_plater.get_partplate_list().get_curr_plate();
        if (plate != nullptr) {
            SliceFailure failure;
            const auto   message = event.format_error_message();
            failure.message        = message.first;
            failure.critical       = event.critical_error();
            failure.project_change = m_project_changes;
            // The same lookup OrcaSlicer's own handler makes: an id that is
            // not a print object's names nothing.
            if (const Print* print = plate->fff_print())
                for (const std::size_t id : message.second)
                    if (const PrintObject* object = print->get_object(ObjectID(id)); object != nullptr && object->model_object() != nullptr)
                        failure.objects.push_back(object->model_object()->id().id);
            m_failures[plate->id().id] = std::move(failure);
        }
    }
    schedule();
}

void PrintIssueMonitor::schedule()
{
    if (m_scheduled)
        return;
    m_scheduled = true;
    wxGetApp().CallAfter([self = std::weak_ptr<PrintIssueMonitor*>(m_self)]() {
        if (const auto monitor = self.lock()) {
            (*monitor)->m_scheduled = false;
            (*monitor)->recompute();
        }
    });
}

void PrintIssueMonitor::refresh_now()
{
    wxASSERT(wxIsMainThread());
    recompute();
}

void PrintIssueMonitor::recompute()
{
    PrintIssueSnapshot next;
    next.session    = m_plater.project_state_session();
    next.generation = m_snapshot.generation;

    PartPlate* plate = m_plater.get_partplate_list().get_curr_plate();
    Print*     print = plate != nullptr ? plate->fff_print() : nullptr;
    if (plate != nullptr && print != nullptr && m_plater.printer_technology() == ptFFF) {
        next.plate   = plate->id().id;
        next.slicing = m_plater.is_background_process_slicing();
        next.sliced  = plate->is_slice_result_valid();
        // A slice that started replaces whatever the last one failed with.
        if ((next.slicing && !m_was_slicing) || next.sliced)
            m_failures.erase(next.plate);
        m_was_slicing = next.slicing;

        // OrcaSlicer applies an edit to the print on a short timer. Until it
        // has, the print describes the project as it was, so nothing read
        // from it is current.
        next.checked = !m_plater.is_background_process_update_scheduled();
        if (next.checked) {
            collect_placement(*plate, next.issues);
            collect_plate_checks(*plate, next.issues);
            collect_validation(*plate, *print, next.issues);
            collect_steps(*plate, *print, next.issues);
            collect_result(*plate, *print, next.issues);
            collect_failure(*plate, next.issues);
        } else {
            // What it last said, kept and marked: not vouched for, not gone.
            // An object that no longer exists has no issue to keep.
            std::set<std::uint64_t> objects, instances;
            for (const ModelObject* object : m_plater.model().objects) {
                objects.insert(object->id().id);
                for (const ModelInstance* instance : object->instances)
                    instances.insert(instance->id().id);
            }
            for (PrintIssue issue : m_snapshot.issues) {
                if (issue.plate != next.plate || (issue.object != 0 && objects.count(issue.object) == 0) ||
                    (issue.instance != 0 && instances.count(issue.instance) == 0))
                    continue;
                issue.stale = true;
                next.issues.push_back(std::move(issue));
            }
        }
    }

    if (next.same_content(m_snapshot))
        return;
    ++next.generation;
    m_snapshot = std::move(next);
    if (m_changed)
        m_changed();
}

// A copy that straddles the plate's edge, which refuses to slice, and a
// filament placed where its nozzle cannot reach. OrcaSlicer stores the first
// on each copy when it updates the print and works out the second, with the
// words for both, in the canvas check its own validation calls.
void PrintIssueMonitor::collect_placement(PartPlate& plate, std::vector<PrintIssue>& issues) const
{
    GLCanvas3D* canvas = m_plater.get_view3D_canvas3D();
    if (canvas == nullptr)
        return;
    ObjectFilamentResults results;
    canvas->check_volumes_outside_state(&results);

    for (const ModelObject* object : m_plater.model().objects)
        for (const ModelInstance* instance : object->instances) {
            if (!object->printable || !instance->printable || instance->print_volume_state != ModelInstancePVS_Partly_Outside)
                continue;
            PrintIssue issue = plate_issue(plate, IssueSource::Placement, IssueSeverity::Blocker, "partly_outside_plate",
                                           get_object_clashed_text());
            set_target(issue, nullptr, instance);
            issue.id += ":" + target_key(issue);
            issues.push_back(std::move(issue));
        }
    if (!get_left_extruder_unprintable_text().empty())
        issues.push_back(plate_issue(plate, IssueSource::Placement, IssueSeverity::Blocker, "left_nozzle_unprintable",
                                     get_left_extruder_unprintable_text()));
    if (!get_right_extruder_unprintable_text().empty())
        issues.push_back(plate_issue(plate, IssueSource::Placement, IssueSeverity::Blocker, "right_nozzle_unprintable",
                                     get_right_extruder_unprintable_text()));
}

// The filament and nozzle checks OrcaSlicer runs on a plate whenever it
// reloads the scene, asked again through the plate's own functions. The first
// two stop a slice; the rest are advice.
void PrintIssueMonitor::collect_plate_checks(PartPlate& plate, std::vector<PrintIssue>& issues) const
{
    if (!plate.has_printable_instances())
        return;
    PresetBundle&            bundle = *wxGetApp().preset_bundle;
    const DynamicPrintConfig config = bundle.full_config();

    if (!plate.check_tpu_printable_status(config, bundle.get_used_tpu_filaments(plate.get_extruders(true))))
        issues.push_back(plate_issue(plate, IssueSource::PlateCheck, IssueSeverity::Blocker, "tpu_filaments",
                                     _u8L("Not support printing 2 or more TPU filaments.")));
    wxString filament_message;
    if (!plate.check_filament_printable(config, filament_message))
        issues.push_back(plate_issue(plate, IssueSource::PlateCheck, IssueSeverity::Blocker, "filament_not_printable",
                                     filament_message.ToUTF8().data()));
    if (!plate.check_mixture_of_pla_and_petg(config))
        issues.push_back(plate_issue(plate, IssueSource::PlateCheck, IssueSeverity::Warning, "pla_and_petg_mixed",
                                     _u8L("PLA and PETG filaments detected in the mixture. Adjust parameters according to the Wiki to ensure print quality.")));
    std::string nozzle_message;
    if (!plate.check_compatible_of_nozzle_and_filament(config, bundle.filament_presets, nozzle_message))
        issues.push_back(plate_issue(plate, IssueSource::PlateCheck, IssueSeverity::Warning, "nozzle_filament_incompatible",
                                     _u8L(nozzle_message)));
    std::string mixture_message;
    if (!plate.check_mixture_filament_compatible(config, mixture_message))
        issues.push_back(plate_issue(plate, IssueSource::PlateCheck, IssueSeverity::Warning, "filament_mixture_incompatible",
                                     _u8L(mixture_message)));

    const GLCanvas3D* canvas = m_plater.get_view3D_canvas3D();
    const Print*      print  = plate.fff_print();
    if (canvas != nullptr && print != nullptr && print->is_step_done(psWipeTower) &&
        !canvas->get_volumes().check_wipe_tower_outside_state(m_plater.build_volume(), plate.get_index()))
        issues.push_back(plate_issue(plate, IssueSource::PlateCheck, IssueSeverity::Warning, "prime_tower_outside",
                                     _u8L("The prime tower extends beyond the plate boundary.")));
}

// OrcaSlicer's own validation of the print, asked again. It holds one error
// and one warning at most; a later check overwrites an earlier warning.
void PrintIssueMonitor::collect_validation(PartPlate& plate, Print& print, std::vector<PrintIssue>& issues)
{
    // While a slice runs the print was valid when it started and any edit
    // that invalidates it would have stopped it, so the last answer stands.
    if (!m_plater.is_background_process_slicing()) {
        const auto started = std::chrono::steady_clock::now();
        StringObjectException warning{};
        Polygons              polygons;
        std::vector<std::pair<Polygon, float>> height_polygons;
        // As BackgroundSlicingProcess::validate does before it asks.
        print.is_BBL_printer() = wxGetApp().preset_bundle->is_bbl_vendor();
        StringObjectException error = print.validate(&warning, &polygons, &height_polygons);
        if (!error.string.empty())
            m_plater.post_process_string_object_exception(error);
        m_last_validate_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        ++m_validate_runs;

        m_validation.clear();
        const auto add = [&](const StringObjectException& result, bool blocker) {
            if (result.string.empty())
                return;
            PrintIssue issue;
            issue.source   = IssueSource::Validation;
            issue.severity = blocker ? IssueSeverity::Blocker : IssueSeverity::Warning;
            issue.code     = check_name(result.type);
            issue.setting  = result.opt_key;
            issue.message  = result.string;
            issue.plate    = plate.id().id;
            set_target(issue, result.object);
            issue.id = std::string("validation:") + (blocker ? "error" : "warning") + ":" + issue.setting + ":" + target_key(issue);
            m_validation.push_back(std::move(issue));
        };
        add(error, true);
        add(warning, false);
    }
    issues.insert(issues.end(), m_validation.begin(), m_validation.end());
}

// Warnings the slicing steps left on the print and its objects. One whose
// step was invalidated stays on the print, marked not current, until the step
// runs again; it is reported the same way.
void PrintIssueMonitor::collect_steps(PartPlate& plate, const Print& print, std::vector<PrintIssue>& issues) const
{
    const auto collect = [&](const PrintStateBase::StateWithWarnings& state, const ModelObject* object, const char* scope, int step) {
        for (const PrintStateBase::Warning& warning : state.warnings) {
            PrintIssue issue;
            issue.source   = IssueSource::SliceStep;
            issue.severity = IssueSeverity::Warning;
            issue.code     = std::string(scope) + "_step_" + std::to_string(step) +
                             (warning.level == PrintStateBase::WarningLevel::CRITICAL ? ":critical" : "");
            issue.message  = warning.message;
            issue.plate    = plate.id().id;
            issue.stale    = !warning.current;
            set_target(issue, object, nullptr);
            // OrcaSlicer's rule: a message id names the warning; without one
            // the message itself does.
            issue.id = "slice_step:" + std::string(scope) + ":" + std::to_string(step) + ":" + target_key(issue) + ":" +
                       (warning.message_id != 0 ? std::to_string(warning.message_id) :
                                                  "m" + std::to_string(std::hash<std::string>{}(warning.message)));
            issues.push_back(std::move(issue));
        }
    };
    for (int step = 0; step < psCount; ++step)
        collect(print.step_state_with_warnings(static_cast<PrintStep>(step)), nullptr, "print", step);
    for (const PrintObject* object : print.objects())
        for (int step = 0; step < posCount; ++step)
            collect(object->step_state_with_warnings(static_cast<PrintObjectStep>(step)), object->model_object(), "object", step);
}

// What a finished slice says about itself. The three that OrcaSlicer's own
// "ready to print" test refuses are blockers: a path outside the build
// volume, a filament a nozzle cannot print where it was placed, and a
// filament that cannot go on this plate's surface.
void PrintIssueMonitor::collect_result(PartPlate& plate, const Print& print, std::vector<PrintIssue>& issues)
{
    const GCodeProcessorResult* result = plate.get_slice_result();
    if (!plate.is_slice_result_valid() || m_plater.is_background_process_slicing() || result == nullptr) {
        m_result = {};
        return;
    }

    if (const ConflictResultOpt conflict = print.get_conflict_result()) {
        PrintIssue issue = plate_issue(plate, IssueSource::SliceResult, IssueSeverity::Warning, "gcode_path_conflict",
                                       print.get_conflict_string());
        // The target the canvas gives its own notice.
        if (const auto* instance = reinterpret_cast<const PrintInstance*>(conflict->_obj2); instance != nullptr) {
            if (instance->model_instance != nullptr)
                set_target(issue, nullptr, instance->model_instance);
            else if (instance->print_object != nullptr)
                set_target(issue, instance->print_object->model_object(), nullptr);
        }
        issue.id += ":" + target_key(issue);
        issues.push_back(std::move(issue));
    }

    if (m_result.result != result || m_result.result_id != result->id) {
        m_result = {result, result->id, {}};
        std::vector<PrintIssue>& found = m_result.issues;

        for (const GCodeProcessorResult::SliceWarning& warning : result->warnings) {
            GCodeProcessorResult::SliceWarning copy = warning;
            const std::string text = Plater::get_slice_warning_string(copy).ToUTF8().data();
            if (text.empty())
                continue; // OrcaSlicer has no words for it and shows nothing itself
            found.push_back(plate_issue(plate, IssueSource::SliceResult, IssueSeverity::Warning,
                                        warning.error_code.empty() ? warning.msg : warning.error_code, text));
        }

        // The result's own flag is only ever written when a sliced file is
        // loaded, so the build volume is asked, as the canvas asks it.
        const BuildVolume&  volume = m_plater.build_volume();
        const BoundingBoxf3 paths  = Workspace::extrusion_bounds(*result);
        if (paths.defined && !volume.all_paths_inside(*result, paths)) {
            const bool too_tall = volume.printable_height() > 0 && paths.max.z() > volume.printable_height() + BuildVolume::BedEpsilon;
            found.push_back(plate_issue(plate, IssueSource::SliceResult, IssueSeverity::Blocker,
                                        too_tall ? "toolpath_above_print_height" : "toolpath_outside_plate",
                                        too_tall ? _u8L("A G-code path goes beyond the max print height.") :
                                                   _u8L("A G-code path goes beyond the plate boundaries.")));
        }

        // Which nozzle each filament was given and could not reach with, in
        // the sentences OrcaSlicer uses for it.
        const bool bbl = wxGetApp().preset_bundle->is_bbl_vendor();
        const auto nozzle = [bbl](int index, bool always_named) {
            if (bbl || always_named)
                return index == 0 ? _u8L("left nozzle") : _u8L("right nozzle");
            return (boost::format(_u8L("Tool %d")) % (index + 1)).str();
        };
        const auto per_nozzle = [&](const std::map<int, std::vector<std::pair<int, int>>>& errors, const char* code,
                                    const char* one, const char* several, bool always_named) {
            std::string text;
            for (const auto& [index, filaments_and_objects] : errors) {
                std::set<int> filaments;
                for (const auto& [filament, object] : filaments_and_objects)
                    filaments.insert(filament);
                const std::string name = nozzle(index, always_named);
                text += (text.empty() ? "" : "\n") +
                        (boost::format(filaments_and_objects.size() == 1 ? _u8L(one) : _u8L(several)) % filament_numbers(filaments) % name % name).str();
            }
            if (!text.empty())
                found.push_back(plate_issue(plate, IssueSource::SliceResult, IssueSeverity::Blocker, code, text));
        };
        if (result->gcode_check_result.error_code & 1)
            per_nozzle(result->gcode_check_result.print_area_error_infos, "nozzle_printable_area",
                       "Filament %s is placed in the %s, but the generated G-code path exceeds the printable range of the %s.",
                       "Filaments %s are placed in the %s, but the generated G-code path exceeds the printable range of the %s.", false);
        if (result->gcode_check_result.error_code & (1 << 1))
            per_nozzle(result->gcode_check_result.print_height_error_infos, "nozzle_printable_height",
                       "Filament %s is placed in the %s, but the generated G-code path exceeds the printable height of the %s.",
                       "Filaments %s are placed in the %s, but the generated G-code path exceeds the printable height of the %s.", true);

        if (const std::vector<int>& unprintable = result->filament_printable_reuslt.conflict_filament; !unprintable.empty()) {
            std::string numbers;
            for (const int filament : unprintable)
                numbers += std::to_string(filament + 1) + " ";
            found.push_back(plate_issue(plate, IssueSource::SliceResult, IssueSeverity::Blocker, "filament_unprintable_on_plate",
                                        (boost::format(_u8L("Filaments %s cannot be printed directly on the surface of this plate.")) % numbers).str()));
        }
    }
    issues.insert(issues.end(), m_result.issues.begin(), m_result.issues.end());
}

// Why the last slice of this plate failed. Only the completion event carries
// it, so it is the one thing kept here rather than read back. It goes stale
// when the project changes and is dropped when a slice starts.
void PrintIssueMonitor::collect_failure(PartPlate& plate, std::vector<PrintIssue>& issues) const
{
    const auto failure = m_failures.find(plate.id().id);
    if (failure == m_failures.end())
        return;
    const auto add = [&](const ModelObject* object) {
        PrintIssue issue = plate_issue(plate, IssueSource::SliceFailure, IssueSeverity::Blocker,
                                       failure->second.critical ? "critical" : "slicing_error", failure->second.message);
        issue.stale = failure->second.project_change != m_project_changes;
        set_target(issue, object, nullptr);
        issue.id = "slice_failure:" + target_key(issue);
        issues.push_back(std::move(issue));
    };
    bool targeted = false;
    for (const std::uint64_t id : failure->second.objects)
        for (const ModelObject* object : m_plater.model().objects)
            if (object->id().id == id) {
                add(object);
                targeted = true;
            }
    if (!targeted)
        add(nullptr);
}

} // namespace Slic3r::GUI::JusPrin::PrintIssues
