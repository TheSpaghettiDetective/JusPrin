#include "PrintIssueMonitor.hpp"

#include "NoticeKinds.hpp"

#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/BackgroundSlicingProcess.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/JusPrin/Workspace/NoticeObserver.hpp"
#include "slic3r/GUI/JusPrin/Workspace/PlaterProjectState.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SliceBounds.hpp"

#include <boost/format.hpp>

#include <algorithm>
#include <chrono>
#include <optional>
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

// `answers` is the notification kind the entry stands in for, or none.
PrintIssue plate_issue(PartPlate& plate, IssueSource source, IssueSeverity severity, std::string code, std::string message,
                       std::optional<NotificationType> answers = std::nullopt)
{
    PrintIssue issue;
    issue.source   = source;
    issue.severity = severity;
    issue.code     = std::move(code);
    issue.message  = std::move(message);
    issue.plate    = plate.id().id;
    issue.id       = std::string(source_name(source)) + ":" + issue.code;
    issue.answers  = answers ? int(*answers) : -1;
    return issue;
}

bool holds(const std::vector<Notice>& held, NotificationType kind)
{
    return std::any_of(held.begin(), held.end(), [kind](const Notice& notice) { return notice.kind == int(kind); });
}

void mix(std::size_t& seed, std::size_t value) { seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2); }

// "1, 3" for filaments 0 and 2: OrcaSlicer counts them from one for people.
std::string filament_numbers(const std::set<int>& filaments)
{
    std::string text;
    for (const int filament : filaments)
        text += (text.empty() ? "" : ", ") + std::to_string(filament + 1);
    return text;
}

} // namespace

PrintIssueMonitor::PrintIssueMonitor(Plater& plater) : m_plater(plater)
{
    wxASSERT(wxIsMainThread());
    // The one thing no notification and no stored state carries: which object
    // a failed slice named, and that it failed at all when OrcaSlicer reports
    // it in a dialog.
    m_plater.Bind(EVT_PROCESS_COMPLETED, &PrintIssueMonitor::on_process_completed, this);
    set_notifications_listener([this] { compare(); });
}

PrintIssueMonitor::~PrintIssueMonitor()
{
    wxASSERT(wxIsMainThread());
    set_notifications_listener({});
    m_plater.Unbind(EVT_PROCESS_COMPLETED, &PrintIssueMonitor::on_process_completed, this);
}

void PrintIssueMonitor::on_process_completed(SlicingProcessCompletedEvent& event)
{
    event.Skip();
    if (!event.error())
        return;
    PartPlate* plate = m_plater.get_partplate_list().get_curr_plate();
    if (plate == nullptr)
        return;
    SliceFailure failure;
    const auto   message = event.format_error_message();
    failure.message  = message.first;
    failure.critical = event.critical_error();
    // The same lookup OrcaSlicer's own handler makes: an id that is not a
    // print object's names nothing.
    if (const Print* print = plate->fff_print())
        for (const std::size_t id : message.second)
            if (const PrintObject* object = print->get_object(ObjectID(id)); object != nullptr && object->model_object() != nullptr)
                failure.objects.push_back(object->model_object()->id().id);
    m_failures[plate->id().id] = std::move(failure);
    m_looked = false; // nothing else may have moved; the next turn rebuilds
}

PrintIssueMonitor::Seen PrintIssueMonitor::look() const
{
    Seen seen;
    seen.session = m_plater.project_state_session();
    if (const PartPlate* plate = m_plater.get_partplate_list().get_curr_plate()) {
        seen.plate  = plate->id().id;
        seen.sliced = plate->is_slice_result_valid();
    }
    seen.checked = !m_plater.is_background_process_update_scheduled();
    seen.slicing = m_plater.is_background_process_slicing();
    m_plater.get_notification_manager()->for_each_notification(
        [&seen](NotificationType type, NotificationManager::NotificationLevel level, const std::string& text) {
            mix(seen.notices, std::size_t(type));
            mix(seen.notices, std::size_t(level));
            mix(seen.notices, std::hash<std::string>{}(text));
        });
    return seen;
}

void PrintIssueMonitor::compare()
{
    ++m_comparisons;
    const Seen now = look();
    // An edit is waiting to be applied: whatever a slice failed with is about
    // the project as it was.
    if (!now.checked)
        for (auto& failure : m_failures)
            failure.second.outdated = true;
    if (m_looked && now == m_seen)
        return;
    m_seen   = now;
    m_looked = true;
    rebuild();
}

void PrintIssueMonitor::refresh_now()
{
    wxASSERT(wxIsMainThread());
    compare();
}

void PrintIssueMonitor::dismiss(int kind)
{
    wxASSERT(wxIsMainThread());
    m_plater.get_notification_manager()->close_notification_of_type(NotificationType(kind));
}

void PrintIssueMonitor::set_ignored(const std::string& issue_id, bool ignored)
{
    wxASSERT(wxIsMainThread());
    // A press is acted on a turn after it was made: by then the issue may
    // have cleared, or OrcaSlicer may have made it a blocker.
    const PrintIssue* issue = m_snapshot.find(issue_id);
    if (issue == nullptr || issue->ignored == ignored || (ignored && issue->severity != IssueSeverity::Warning))
        return;
    std::set<std::string>& ids = m_ignored[m_snapshot.plate];
    if (ignored)
        ids.insert(issue_id);
    else
        ids.erase(issue_id);
    mark_ignored(m_snapshot.issues, ids, false);
    ++m_snapshot.generation;
    if (m_changed)
        m_changed();
}

std::vector<Notice> PrintIssueMonitor::held() const
{
    std::vector<Notice> notices;
    // OrcaSlicer's own first line on an error or a warning.
    const std::string labels[] = {_u8L("Error:"), _u8L("Warning:"), _u8L("WARNING:")};
    m_plater.get_notification_manager()->for_each_notification(
        [&](NotificationType type, NotificationManager::NotificationLevel level, const std::string& text) {
            using Level = NotificationManager::NotificationLevel;
            Notice notice;
            notice.kind      = int(type);
            notice.kind_name = notice_kind_name(type);
            notice.route     = notice_route(type);
            notice.level     = level == Level::ErrorNotificationLevel ? NoticeLevel::Error :
                               level == Level::WarningNotificationLevel || level == Level::SeriousWarningNotificationLevel ? NoticeLevel::Warning :
                                                                                                                             NoticeLevel::Info;
            notice.text      = text;
            notice.body      = text;
            notice.several   = notice_comes_in_several(type);
            if (const std::size_t end = text.find('\n'); end != std::string::npos)
                for (const std::string& label : labels)
                    if (text.compare(0, end, label) == 0)
                        notice.body = text.substr(end + 1);
            notices.push_back(std::move(notice));
        });
    return notices;
}

void PrintIssueMonitor::rebuild()
{
    ++m_rebuilds;
    PrintIssueSnapshot next;
    next.session    = m_plater.project_state_session();
    next.generation = m_snapshot.generation;
    if (next.session != m_snapshot.session) {
        // Another project: nothing read from the last one describes it.
        m_failures.clear();
        m_validation.clear();
        m_result = {};
        m_ignored.clear();
    }

    const std::vector<Notice> notices = held();
    for (const Notice& notice : notices)
        if ((notice.route == NoticeRoute::Result || notice.route == NoticeRoute::Progress) && !notice.text.empty())
            next.notices.push_back(notice);

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
        // has, the print and the notifications describe the project as it
        // was, so nothing read from them is current.
        next.checked = !m_plater.is_background_process_update_scheduled();
        if (next.checked) {
            std::vector<PrintIssue> found;
            find_placement(*plate, notices, found);
            find_validation(*plate, *print, notices, found);
            if (holds(notices, NotificationType::SlicingWarning))
                find_steps(*plate, *print, found);
            read_result(*plate, *print, found);
            read_failure(*plate, found);
            next.issues = issues_from(notices, std::move(found), next.plate);
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
        mark_ignored(next.issues, m_ignored[next.plate], next.checked);
    }

    if (next.same_content(m_snapshot))
        return;
    ++next.generation;
    m_snapshot = std::move(next);
    if (m_changed)
        m_changed();
    if (m_work_changed)
        m_work_changed();
}

// Which copy straddles the plate's edge, and which object sits where its
// nozzle cannot reach. OrcaSlicer stores the first on each copy when it
// updates the print and works out the second, with the words for both, in the
// canvas check its own validation calls. Asked only while OrcaSlicer holds
// the notification the answer belongs to.
void PrintIssueMonitor::find_placement(PartPlate& plate, const std::vector<Notice>& held, std::vector<PrintIssue>& found) const
{
    GLCanvas3D* canvas = m_plater.get_view3D_canvas3D();
    const bool  edge   = holds(held, NotificationType::PlaterError);
    const bool  left   = holds(held, NotificationType::LeftExtruderUnprintableError);
    const bool  right  = holds(held, NotificationType::RightExtruderUnprintableError);
    if (canvas == nullptr || !(edge || left || right))
        return;
    ObjectFilamentResults results;
    canvas->check_volumes_outside_state(&results);

    if (edge)
        for (const ModelObject* object : m_plater.model().objects)
            for (const ModelInstance* instance : object->instances) {
                if (!object->printable || !instance->printable || instance->print_volume_state != ModelInstancePVS_Partly_Outside)
                    continue;
                PrintIssue issue = plate_issue(plate, IssueSource::Placement, IssueSeverity::Blocker, "partly_outside_plate",
                                               get_object_clashed_text(), NotificationType::PlaterError);
                set_target(issue, nullptr, instance);
                issue.id += ":" + target_key(issue);
                found.push_back(std::move(issue));
            }

    // OrcaSlicer's result names each object and, where the person mapped
    // filaments to nozzles by hand, the nozzle (1 left, 2 right) that cannot
    // reach it. With automatic mapping the object is out of reach of both.
    const auto reach = [&](bool wanted, int nozzle, const char* code, const std::string& words, NotificationType kind) {
        if (!wanted || words.empty())
            return;
        std::size_t named = 0;
        for (const ObjectFilamentInfo& entry : results.object_filaments) {
            const bool by_hand = std::any_of(entry.manual_filaments.begin(), entry.manual_filaments.end(),
                                             [nozzle](const std::pair<const int, int>& mapped) { return mapped.second == nozzle; });
            if (entry.object == nullptr ||
                !(results.mode == FilamentMapMode::fmmManual ? by_hand : !entry.auto_filaments.empty()))
                continue;
            PrintIssue issue = plate_issue(plate, IssueSource::Placement, IssueSeverity::Blocker, code, words, kind);
            set_target(issue, entry.object, nullptr);
            issue.id += ":" + target_key(issue);
            found.push_back(std::move(issue));
            ++named;
        }
        if (named == 0)
            found.push_back(plate_issue(plate, IssueSource::Placement, IssueSeverity::Blocker, code, words, kind));
    };
    reach(left, 1, "left_nozzle_unprintable", get_left_extruder_unprintable_text(), NotificationType::LeftExtruderUnprintableError);
    reach(right, 2, "right_nozzle_unprintable", get_right_extruder_unprintable_text(), NotificationType::RightExtruderUnprintableError);
}

// OrcaSlicer's own validation of the print, asked again for the object and
// the setting its notification does not carry. It holds one error and one
// warning at most; a later check overwrites an earlier warning.
void PrintIssueMonitor::find_validation(PartPlate& plate, Print& print, const std::vector<Notice>& held, std::vector<PrintIssue>& found)
{
    if (!holds(held, NotificationType::ValidateError) && !holds(held, NotificationType::ValidateWarning)) {
        m_validation.clear();
        return;
    }
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
            issue.answers  = int(blocker ? NotificationType::ValidateError : NotificationType::ValidateWarning);
            set_target(issue, result.object);
            issue.id = std::string("validation:") + (blocker ? "error" : "warning") + ":" + issue.setting + ":" + target_key(issue);
            m_validation.push_back(std::move(issue));
        };
        add(error, true);
        add(warning, false);
    }
    found.insert(found.end(), m_validation.begin(), m_validation.end());
}

// The warnings the slicing steps left on the print and its objects, for the
// object each is about. One whose step was invalidated stays on the print,
// marked not current, until the step runs again; it is reported the same way.
void PrintIssueMonitor::find_steps(PartPlate& plate, const Print& print, std::vector<PrintIssue>& found) const
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
            issue.answers  = int(NotificationType::SlicingWarning);
            set_target(issue, object, nullptr);
            // OrcaSlicer's rule: a message id names the warning; without one
            // the message itself does.
            issue.id = "slice_step:" + std::string(scope) + ":" + std::to_string(step) + ":" + target_key(issue) + ":" +
                       (warning.message_id != 0 ? std::to_string(warning.message_id) :
                                                  "m" + std::to_string(std::hash<std::string>{}(warning.message)));
            found.push_back(std::move(issue));
        }
    };
    for (int step = 0; step < psCount; ++step)
        collect(print.step_state_with_warnings(static_cast<PrintStep>(step)), nullptr, "print", step);
    for (const PrintObject* object : print.objects())
        for (int step = 0; step < posCount; ++step)
            collect(object->step_state_with_warnings(static_cast<PrintObjectStep>(step)), object->model_object(), "object", step);
}

// What a finished slice says about itself. OrcaSlicer words these only when
// its preview loads the slice, which here means opening Check print, while it
// refuses to print from the moment the slice ends. So they are read from the
// result whether or not a notification exists, and each stands in for the
// notification OrcaSlicer gives it later.
void PrintIssueMonitor::read_result(PartPlate& plate, const Print& print, std::vector<PrintIssue>& found)
{
    const GCodeProcessorResult* result = plate.get_slice_result();
    if (!plate.is_slice_result_valid() || m_plater.is_background_process_slicing() || result == nullptr) {
        m_result = {};
        return;
    }
    const std::size_t first = found.size();

    if (const ConflictResultOpt conflict = print.get_conflict_result()) {
        PrintIssue issue = plate_issue(plate, IssueSource::SliceResult, IssueSeverity::Warning, "gcode_path_conflict",
                                       print.get_conflict_string(), NotificationType::SlicingSeriousWarning);
        // The target the canvas gives its own notice.
        if (const auto* instance = reinterpret_cast<const PrintInstance*>(conflict->_obj2); instance != nullptr) {
            if (instance->model_instance != nullptr)
                set_target(issue, nullptr, instance->model_instance);
            else if (instance->print_object != nullptr)
                set_target(issue, instance->print_object->model_object(), nullptr);
        }
        issue.id += ":" + target_key(issue);
        found.push_back(std::move(issue));
    }

    if (m_result.result != result || m_result.result_id != result->id) {
        m_result = {result, result->id, {}};
        std::vector<PrintIssue>& read = m_result.issues;

        for (const GCodeProcessorResult::SliceWarning& warning : result->warnings) {
            GCodeProcessorResult::SliceWarning copy = warning;
            const std::string text = Plater::get_slice_warning_string(copy).ToUTF8().data();
            if (text.empty())
                continue; // OrcaSlicer has no words for it and shows nothing itself
            read.push_back(plate_issue(plate, IssueSource::SliceResult, IssueSeverity::Warning,
                                       warning.error_code.empty() ? warning.msg : warning.error_code, text));
        }

        // The result's own "outside" flag is only ever written when a sliced
        // file is loaded, so the build volume is asked, as the canvas asks it
        // for its notice. Whether that stops a print is OrcaSlicer's own
        // post-slice check, which allows a path 2 mm past the edge.
        const BuildVolume&  volume = m_plater.build_volume();
        const BoundingBoxf3 paths  = Workspace::extrusion_bounds(*result);
        if (paths.defined && !volume.all_paths_inside(*result, paths)) {
            const bool too_tall = volume.printable_height() > 0 && paths.max.z() > volume.printable_height() + BuildVolume::BedEpsilon;
            const bool refused  = result->toolpath_outside || (result->gcode_check_result.error_code & ((1 << 2) | (1 << 3))) != 0;
            read.push_back(plate_issue(plate, IssueSource::SliceResult, refused ? IssueSeverity::Blocker : IssueSeverity::Warning,
                                       too_tall ? "toolpath_above_print_height" : "toolpath_outside_plate",
                                       too_tall ? _u8L("A G-code path goes beyond the max print height.") :
                                                  _u8L("A G-code path goes beyond the plate boundaries."),
                                       NotificationType::SlicingError));
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
                                    const char* one, const char* several, bool always_named, NotificationType kind) {
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
                read.push_back(plate_issue(plate, IssueSource::SliceResult, IssueSeverity::Blocker, code, text, kind));
        };
        if (result->gcode_check_result.error_code & 1)
            per_nozzle(result->gcode_check_result.print_area_error_infos, "nozzle_printable_area",
                       "Filament %s is placed in the %s, but the generated G-code path exceeds the printable range of the %s.",
                       "Filaments %s are placed in the %s, but the generated G-code path exceeds the printable range of the %s.", false,
                       NotificationType::BBLSliceLimitError);
        if (result->gcode_check_result.error_code & (1 << 1))
            per_nozzle(result->gcode_check_result.print_height_error_infos, "nozzle_printable_height",
                       "Filament %s is placed in the %s, but the generated G-code path exceeds the printable height of the %s.",
                       "Filaments %s are placed in the %s, but the generated G-code path exceeds the printable height of the %s.", true,
                       NotificationType::BBLSliceMultiExtruderHeightOutside);

        if (const std::vector<int>& unprintable = result->filament_printable_reuslt.conflict_filament; !unprintable.empty()) {
            std::string numbers;
            for (const int filament : unprintable)
                numbers += std::to_string(filament + 1) + " ";
            read.push_back(plate_issue(plate, IssueSource::SliceResult, IssueSeverity::Blocker, "filament_unprintable_on_plate",
                                       (boost::format(_u8L("Filaments %s cannot be printed directly on the surface of this plate.")) % numbers).str(),
                                       NotificationType::BBLBedFilamentIncompatible));
        }
    }
    found.insert(found.end(), m_result.issues.begin(), m_result.issues.end());
    for (std::size_t index = first; index < found.size(); ++index)
        found[index].unprompted = true;
}

// Why the last slice of this plate failed, from the completion event: a
// critical failure is shown in a dialog and leaves no notification, and no
// notification says which object the failure named. It is marked once the
// project is edited and dropped when a slice starts.
void PrintIssueMonitor::read_failure(PartPlate& plate, std::vector<PrintIssue>& found) const
{
    const auto failure = m_failures.find(plate.id().id);
    if (failure == m_failures.end())
        return;
    const auto add = [&](const ModelObject* object) {
        PrintIssue issue = plate_issue(plate, IssueSource::SliceFailure, IssueSeverity::Blocker,
                                       failure->second.critical ? "critical" : "slicing_error", failure->second.message,
                                       NotificationType::SlicingError);
        issue.stale      = failure->second.outdated;
        issue.unprompted = true;
        set_target(issue, object, nullptr);
        issue.id = "slice_failure:" + target_key(issue);
        found.push_back(std::move(issue));
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
