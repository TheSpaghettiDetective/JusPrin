#include "OrcaWorkspaceAdapter.hpp"
#include "OrcaSettings.hpp"
#include "HostLocale.hpp"

#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include <boost/filesystem.hpp>

#include <wx/thread.h>

#include <algorithm>
#include <cctype>
#include <set>

namespace Slic3r::GUI::JusPrin::Workspace {

namespace {

ObjectTransform transform_of(const ModelInstance& instance)
{
    const Vec3d position = instance.get_offset();
    const Vec3d rotation = instance.get_rotation();
    const Vec3d scale    = instance.get_scaling_factor();
    return {{position.x(), position.y(), position.z()}, {rotation.x(), rotation.y(), rotation.z()}, {scale.x(), scale.y(), scale.z()}};
}

WorkspaceObject project_object(ProjectSessionId session, const ModelObject& object, PartPlate& plate, int object_index)
{
    WorkspaceObject projected;
    projected.id   = ObjectId(session, object.id().id);
    projected.name = object.name;
    projected.instances.reserve(object.instances.size());
    for (std::size_t instance_index = 0; instance_index < object.instances.size(); ++instance_index)
        if (plate.contain_instance(object_index, static_cast<int>(instance_index)))
            projected.instances.emplace_back(transform_of(*object.instances[instance_index]));
    return projected;
}

WorkspaceChangeReasons workspace_reasons(ProjectStateChangeReason reasons)
{
    WorkspaceChangeReasons result = WorkspaceChangeReasons::None;
    const auto contains = [reasons](ProjectStateChangeReason reason) {
        return (static_cast<std::uint32_t>(reasons) & static_cast<std::uint32_t>(reason)) != 0;
    };
    if (contains(ProjectStateChangeReason::Selection))
        result |= WorkspaceChangeReasons::Selection;
    if (contains(ProjectStateChangeReason::Objects))
        result |= WorkspaceChangeReasons::Contents;
    if (contains(ProjectStateChangeReason::History))
        result |= WorkspaceChangeReasons::History;
    if (contains(ProjectStateChangeReason::Transform))
        result |= WorkspaceChangeReasons::Transform;
    if (contains(ProjectStateChangeReason::Plates))
        result |= WorkspaceChangeReasons::Plates;
    if (contains(ProjectStateChangeReason::Project))
        result |= WorkspaceChangeReasons::Project;
    if (contains(ProjectStateChangeReason::Settings))
        result |= WorkspaceChangeReasons::Settings;
    return result;
}

bool blank(const std::string& value)
{
    return value.empty() || std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
}

// Time and material for one plate, read from the plate's own G-code result the
// same way Orca's preview and cost readouts do: volume per extruder times that
// filament's density and price. Nothing here is computed by JusPrin, so the
// card can never disagree with the number the slicer shows.
std::optional<SliceEstimate> estimate_of(const GCodeProcessorResult& result)
{
    SliceEstimate estimate;
    const auto& statistics = result.print_statistics;
    const auto& mode = statistics.modes[static_cast<std::size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];
    estimate.print_time_seconds = mode.time > 0.f ? static_cast<std::uint32_t>(mode.time) : 0u;

    double cost = 0.0;
    bool   every_filament_priced = true;
    for (const auto& [extruder, volume] : statistics.total_volumes_per_extruder) {
        // A slice that used an extruder Orca has no density for cannot be
        // weighed, and a partial weight presented as the total is worse than
        // no weight at all -- so the whole estimate goes.
        if (extruder >= result.filament_densities.size()) return std::nullopt;
        const double grams = volume * result.filament_densities[extruder] * 0.001;
        estimate.material_grams += grams;
        // A filament profile with no price reports zero, which is not a price.
        if (extruder < result.filament_costs.size() && result.filament_costs[extruder] > 0.f)
            cost += grams * result.filament_costs[extruder] * 0.001;
        else
            every_filament_priced = false;
    }
    // Money is the cost of the whole print or it is not shown. With one spool
    // priced and another not, a "total" would silently leave material out.
    estimate.has_cost      = every_filament_priced && cost > 0.0;
    estimate.material_cost = estimate.has_cost ? cost : 0.0;
    // A slice that produced no material is not an estimate worth a row.
    if (estimate.print_time_seconds == 0 && estimate.material_grams <= 0.0) return std::nullopt;
    return estimate;
}

// Every process setting whose value in force differs from the preset it was
// loaded from -- the card's "N changes from preset". Keys Orca reports but no
// longer defines are skipped rather than shown without a label.
std::vector<PresetDelta> preset_deltas_of(const PresetCollection& prints, const std::vector<std::string>& dirty)
{
    std::vector<PresetDelta> result;
    result.reserve(dirty.size());
    const DynamicPrintConfig& edited = prints.get_edited_preset().config;
    const DynamicPrintConfig& saved  = prints.get_selected_preset().config;
    for (const std::string& key : dirty) {
        const ConfigOptionDef* definition = print_config_def.get(key);
        const ConfigOption*    before     = saved.option(key);
        const ConfigOption*    after      = edited.option(key);
        if (definition == nullptr || before == nullptr || after == nullptr) continue;
        const std::string& label = definition->full_label.empty() ? definition->label : definition->full_label;
        result.push_back({key, label.empty() ? key : label, before->serialize(), after->serialize()});
    }
    return result;
}

// The settings edited between two readings of one preset. A key's value in
// force is its delta's value, or the preset's own value when it has no delta.
std::vector<WorkspaceEdit> setting_edits(const std::vector<PresetDelta>& before, const std::vector<PresetDelta>& after,
                                         const PresetCollection& collection)
{
    const std::string& preset = collection.get_edited_preset().name;
    const auto find = [](const std::vector<PresetDelta>& deltas, const std::string& key) {
        const auto found = std::find_if(deltas.begin(), deltas.end(), [&key](const PresetDelta& delta) { return delta.key == key; });
        return found == deltas.end() ? nullptr : &*found;
    };
    std::vector<WorkspaceEdit> edits;
    for (const PresetDelta& delta : after) {
        const PresetDelta* earlier = find(before, delta.key);
        const std::string& was     = earlier != nullptr ? earlier->value : delta.preset;
        if (was != delta.value)
            edits.push_back({EditKind::Setting, EditActor::Person, delta.label, was, delta.value, preset});
    }
    // A delta disappears when the setting is put back to the preset's value,
    // and also when the preset is saved with the value in it -- which changes
    // nothing in force. So the value now in force decides.
    for (const PresetDelta& delta : before)
        if (find(after, delta.key) == nullptr) {
            const ConfigOption* option = collection.get_edited_preset().config.option(delta.key);
            const std::string   now    = option != nullptr ? option->serialize() : delta.preset;
            if (now != delta.value)
                edits.push_back({EditKind::Setting, EditActor::Person, delta.label, delta.value, now, preset});
        }
    return edits;
}

} // namespace

OrcaWorkspaceAdapter::OrcaWorkspaceAdapter(Plater& plater) : m_plater(plater)
{
    wxASSERT(wxIsMainThread());
    m_session = ProjectSessionId(m_plater.project_state_session());
    m_project_subscription = m_plater.subscribe_project_state(
        [this](const ProjectStateChanged& change) { on_project_state_changed(change); });
    // Slicing changes what a plate holds without changing the project, so it
    // raises no ProjectStateChanged and consumers would keep reporting the
    // plate unsliced until something unrelated moved. The header already
    // listens to this event; the workspace has to as well, or an estimate only
    // appears after the next click.
    m_plater.Bind(EVT_SLICE_STATUS_CHANGED, &OrcaWorkspaceAdapter::on_slice_status_changed, this);
    m_known_slice_state = current_slice_state();
    remember_current_ids();
    remember_history();
    record_settings_edits(/*report=*/false);
}

OrcaWorkspaceAdapter::~OrcaWorkspaceAdapter()
{
    wxASSERT(wxIsMainThread());
    m_plater.Unbind(EVT_SLICE_STATUS_CHANGED, &OrcaWorkspaceAdapter::on_slice_status_changed, this);
    m_project_subscription.reset();
}

OrcaWorkspaceAdapter::SliceState OrcaWorkspaceAdapter::current_slice_state() const
{
    SliceState state;
    // Whether a slice is running is part of what the plate holds, not merely
    // progress: starting one on an already-invalid plate changes neither the
    // valid flag nor the result id, so without it that transition publishes
    // nothing and no consumer ever learns the slice began.
    const bool slicing = m_plater.is_background_process_slicing();
    PartPlateList& plates = m_plater.get_partplate_list();
    for (int index = 0; index < plates.get_plate_count(); ++index) {
        PartPlate* plate = plates.get_plate(index);
        if (plate == nullptr) continue;
        const bool sliced = plate->is_slice_result_valid();
        const std::uint64_t result = sliced && !slicing && plate->get_slice_result() ?
            plate->get_slice_result()->id : 0;
        state.emplace(plate->id().id, PlateSlice{sliced, result, slicing});
    }
    return state;
}

void OrcaWorkspaceAdapter::on_slice_status_changed(wxCommandEvent& event)
{
    event.Skip();
    wxASSERT(wxIsMainThread());
    // This event also fires whenever the native toolbar re-evaluates whether
    // Slice and Print are enabled, which happens on ordinary selection and
    // object changes. Publishing on all of those would advance the revision --
    // and add a history entry -- for nothing. Only a real change in what the
    // plates hold is a workspace change.
    SliceState state = current_slice_state();
    if (state == m_known_slice_state)
        return;
    // A plate that held a slice and no longer does was invalidated by whatever
    // the person just did. Recording it here, at the transition, is the only
    // moment the cause is still known.
    for (const auto& [id, now] : state) {
        const auto before = m_known_slice_state.find(id);
        if (before != m_known_slice_state.end() && before->second.sliced && !now.sliced)
            m_invalidated_by[id] = m_last_change_reason;
        else if (now.sliced)
            m_invalidated_by.erase(id);
    }
    m_known_slice_state = std::move(state);
    publish_change(WorkspaceChangeReasons::Plates);
}

WorkspaceSnapshot OrcaWorkspaceAdapter::snapshot() const
{
    wxASSERT(wxIsMainThread());
    WorkspaceSnapshot result;
    result.session  = m_session;
    result.revision = m_changes.revision();

    // Read once: regional settings do not change while the app runs, and this
    // runs on every selection change.
    static const std::string currency = host_currency_code();
    result.currency = currency;

    result.setup.project_name  = m_plater.get_project_name().ToUTF8().data();
    result.setup.project_dirty = m_plater.is_project_dirty();
    if (const PresetBundle* presets = wxGetApp().preset_bundle; presets != nullptr) {
        result.setup.printer_preset = presets->printers.get_selected_preset().label(false);
        if (presets->printers.get_edited_preset().printer_technology() == ptFFF) {
            result.setup.process_preset = presets->prints.get_edited_preset().name;
            // One diff, used twice: snapshot() runs on every selection change.
            const std::vector<std::string> dirty = presets->prints.current_dirty_options();
            result.setup.process_preset_dirty = !dirty.empty();
            result.preset_deltas = preset_deltas_of(presets->prints, dirty);
        }
        if (!presets->filament_presets.empty())
            result.setup.filament_preset = presets->filament_presets.front();
    }

    PartPlateList& plate_list = m_plater.get_partplate_list();
    const int active_index = plate_list.get_curr_plate_index();
    result.plates.reserve(plate_list.get_plate_count());
    for (int index = 0; index < plate_list.get_plate_count(); ++index) {
        PartPlate* plate = plate_list.get_plate(index);
        WorkspacePlate projected_plate;
        projected_plate.id     = PlateId(m_session, plate->id().id);
        projected_plate.name   = plate->get_plate_name().empty() ? "Plate " + std::to_string(index + 1) : plate->get_plate_name();
        projected_plate.active = index == active_index;
        projected_plate.sliced = plate->is_slice_result_valid();
        const std::uint64_t plate_key = plate->id().id;
        if (projected_plate.sliced && !m_plater.is_background_process_slicing() && plate->get_slice_result()) {
            projected_plate.slice_result_id = plate->get_slice_result()->id;
            projected_plate.estimate        = estimate_of(*plate->get_slice_result());
            projected_plate.estimate_status = EstimateStatus::Current;
            // The figure this plate can currently defend, kept so a slice in
            // flight or an invalidated one still has something to show.
            if (projected_plate.estimate) m_last_estimate[plate_key] = *projected_plate.estimate;
        } else if (const auto remembered = m_last_estimate.find(plate_key); remembered != m_last_estimate.end()) {
            projected_plate.estimate        = remembered->second;
            projected_plate.estimate_status = m_plater.is_background_process_slicing() ?
                EstimateStatus::Recomputing : EstimateStatus::Stale;
            if (projected_plate.estimate_status == EstimateStatus::Stale) {
                if (const auto why = m_invalidated_by.find(plate_key); why != m_invalidated_by.end())
                    projected_plate.invalidated_by = why->second;
            }
        }
        const ModelObjectPtrs& objects = m_plater.model().objects;
        for (std::size_t object_index = 0; object_index < objects.size(); ++object_index) {
            WorkspaceObject object = project_object(m_session, *objects[object_index], *plate, static_cast<int>(object_index));
            if (!object.instances.empty())
                projected_plate.objects.emplace_back(std::move(object));
        }
        if (projected_plate.active)
            result.active_plate = projected_plate.id;
        result.plates.emplace_back(std::move(projected_plate));
    }

    remember_current_ids();

    Selection& selection = m_plater.canvas3D()->get_selection();
    if (selection.is_empty()) {
        result.selection_status = SelectionStatus::None;
    } else if (selection.is_single_full_object()) {
        const int index = selection.get_object_idx();
        if (index >= 0 && index < static_cast<int>(m_plater.model().objects.size())) {
            result.selection_status = SelectionStatus::Objects;
            result.selected_objects.emplace_back(m_session, m_plater.model().objects[index]->id().id);
        } else {
            result.selection_status = SelectionStatus::Unsupported;
        }
    } else if (selection.is_multiple_full_object()) {
        std::set<ObjectId> selected;
        for (const auto& object_instance : selection.get_selected_object_instances()) {
            const int index = object_instance.first;
            if (index >= 0 && index < static_cast<int>(m_plater.model().objects.size()))
                selected.emplace(m_session, m_plater.model().objects[index]->id().id);
        }
        result.selection_status = SelectionStatus::Objects;
        result.selected_objects.assign(selected.begin(), selected.end());
    } else {
        result.selection_status = SelectionStatus::Unsupported;
    }

    result.can_undo = m_plater.can_undo_project();
    result.can_redo = m_plater.can_redo_project();
    return result;
}

CommandResult OrcaWorkspaceAdapter::select_object(ObjectId id)
{
    wxASSERT(wxIsMainThread());
    const auto object = resolve(id);
    if (!object)
        return id_error(id);

    const WorkspaceSnapshot before = snapshot();
    if (before.selection_status == SelectionStatus::Objects && before.selected_objects == std::vector<ObjectId>{id})
        return CommandResult::failure(WorkspaceError::NoChange, "Object is already selected");
    if (!m_plater.select_object(object->index))
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "Object could not be selected");
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::rename_object(ObjectId id, const std::string& name)
{
    wxASSERT(wxIsMainThread());
    if (blank(name))
        return CommandResult::failure(WorkspaceError::InvalidArgument, "Object name cannot be empty");
    const auto object = resolve(id);
    if (!object)
        return id_error(id);
    if (m_plater.model().objects[object->index]->name == name)
        return CommandResult::failure(WorkspaceError::NoChange, "Object already has that name");
    if (!m_plater.rename_object(object->index, name))
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "Object could not be renamed");
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::duplicate_object(ObjectId id)
{
    wxASSERT(wxIsMainThread());
    const auto object = resolve(id);
    if (!object)
        return id_error(id);

    const int new_index = m_plater.duplicate_object(object->index);
    if (new_index < 0 || new_index >= static_cast<int>(m_plater.model().objects.size()))
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "Object could not be duplicated");

    const ObjectId new_id(m_session, m_plater.model().objects[new_index]->id().id);
    m_known_object_ids.insert(new_id.value());
    return CommandResult::success(new_id);
}

CommandResult OrcaWorkspaceAdapter::remove_object(ObjectId id)
{
    wxASSERT(wxIsMainThread());
    const auto object = resolve(id);
    if (!object)
        return id_error(id);
    if (!m_plater.delete_object(object->index))
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "Object could not be removed");
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::undo()
{
    wxASSERT(wxIsMainThread());
    if (!m_plater.can_undo_project() || !m_plater.undo_project())
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "Nothing to undo");
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::redo()
{
    wxASSERT(wxIsMainThread());
    if (!m_plater.can_redo_project() || !m_plater.redo_project())
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "Nothing to redo");
    return CommandResult::success();
}

std::string OrcaWorkspaceAdapter::auxiliary_data_dir() const
{
    wxASSERT(wxIsMainThread());
    // The model's auxiliary temp dir is extracted from and repacked into the
    // project 3MF by Orca's own load/store paths, and its path changes when a
    // project is loaded or created — resolve it fresh on every call.
    return m_plater.model().get_auxiliary_file_temp_path();
}

CommandResult OrcaWorkspaceAdapter::export_project_archive(const std::string& file_path)
{
    wxASSERT(wxIsMainThread());
    // Exporting renders plate thumbnails; before the canvas has initialized
    // its GL state (early startup), those render calls go through unloaded
    // function pointers and crash. Refuse honestly instead.
    GLCanvas3D* canvas = m_plater.get_view3D_canvas3D();
    if (canvas == nullptr || !canvas->is_initialized())
        return CommandResult::failure(WorkspaceError::UnavailableOperation,
                                      "The canvas is not ready to render the archive's thumbnails yet");
    // SkipAuxiliary keeps consumer files out of the archive; the remaining
    // strategy matches an ordinary project save.
    const SaveStrategy strategy = SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::ShareMesh |
                                  SaveStrategy::SkipAuxiliary;
    if (m_plater.export_3mf(boost::filesystem::path(file_path), strategy) < 0)
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "The project archive could not be written");
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::import_model(const std::string& file_path)
{
    wxASSERT(wxIsMainThread());
    boost::system::error_code ec;
    if (!boost::filesystem::is_regular_file(file_path, ec) || boost::filesystem::file_size(file_path, ec) == 0)
        return CommandResult::failure(WorkspaceError::InvalidArgument, "The model file does not exist");

    const std::size_t before = m_plater.model().objects.size();
    {
        // One coalesced, undoable manufacturing change: the snapshot's History
        // change and the importer's Objects change commit as a single workspace
        // revision. LoadModel is the additive, geometry-only strategy — it adds
        // objects to the current project rather than replacing it.
        ProjectStateTransaction transaction = m_plater.project_state_transaction();
        m_plater.take_snapshot("Import model");
        m_plater.load_files(std::vector<boost::filesystem::path>{boost::filesystem::path(file_path)},
                            LoadStrategy::LoadModel);
    }
    if (m_plater.model().objects.size() <= before)
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "The model could not be imported");

    const ObjectId new_id(m_session, m_plater.model().objects.back()->id().id);
    m_known_object_ids.insert(new_id.value());
    return CommandResult::success(new_id);
}

WorkspaceSubscription OrcaWorkspaceAdapter::subscribe(WorkspaceChangedCallback callback)
{
    wxASSERT(wxIsMainThread());
    return m_changes.subscribe(std::move(callback));
}

std::optional<OrcaWorkspaceAdapter::ResolvedObject> OrcaWorkspaceAdapter::resolve(ObjectId id) const
{
    if (!id || id.session() != m_session)
        return std::nullopt;
    const ModelObjectPtrs& objects = m_plater.model().objects;
    for (std::size_t index = 0; index < objects.size(); ++index)
        if (objects[index]->id().id == id.value())
            return ResolvedObject{index};
    return std::nullopt;
}

CommandResult OrcaWorkspaceAdapter::id_error(ObjectId id) const
{
    if (!id)
        return CommandResult::failure(WorkspaceError::InvalidId, "Object ID is invalid");
    if (id.session() != m_session)
        return CommandResult::failure(WorkspaceError::StaleId, "Object ID belongs to an earlier project session");
    const WorkspaceError error = m_known_object_ids.count(id.value()) > 0 ? WorkspaceError::StaleId : WorkspaceError::MissingObject;
    return CommandResult::failure(error, "Object does not exist in the current project session");
}

void OrcaWorkspaceAdapter::on_project_state_changed(const ProjectStateChanged& change)
{
    wxASSERT(wxIsMainThread());
    const auto touched = [&](ProjectStateChangeReason reason) {
        return (static_cast<std::uint32_t>(change.reasons) & static_cast<std::uint32_t>(reason)) != 0;
    };
    if (change.project_replaced) {
        m_session = ProjectSessionId(change.project_session);
        m_known_object_ids.clear();
        // A new or opened project starts its own history and presets; nothing
        // in it is an edit.
        remember_history();
        record_settings_edits(/*report=*/false);
    } else {
        record_history_edits();
        // Undo and redo restore the configuration they saved; that is part of
        // the undo already recorded, so the baseline just follows it.
        if (touched(ProjectStateChangeReason::Settings))
            record_settings_edits(/*report=*/!m_plater.inside_snapshot_capture());
        if (touched(ProjectStateChangeReason::Modified))
            publish_edit({EditKind::Mark});
    }
    remember_current_ids();

    const WorkspaceChangeReasons reasons = workspace_reasons(change.reasons);
    // An undo step or a dirty mark alone is no workspace change, and must not
    // clear the reason a slice about to be invalidated will report.
    if (reasons == WorkspaceChangeReasons::None)
        return;
    // Orca invalidates the slice as a consequence of this change and only then
    // posts EVT_SLICE_STATUS_CHANGED, so the reason is still the newest one
    // when the slice watcher runs. Naming only the actions a person would
    // recognise; anything else leaves the card saying nothing rather than
    // attributing the loss to something the reader did not do.
    if (touched(ProjectStateChangeReason::Transform))     m_last_change_reason = "you moved the object";
    else if (touched(ProjectStateChangeReason::Objects))  m_last_change_reason = "the objects changed";
    else if (touched(ProjectStateChangeReason::Settings)) m_last_change_reason = "a setting changed";
    else                                                  m_last_change_reason.clear();

    publish_change(reasons);
}

void OrcaWorkspaceAdapter::remember_history()
{
    const UndoRedo::Stack&                  stack     = m_plater.undo_redo_stack_main();
    const std::vector<UndoRedo::Snapshot>& snapshots = stack.snapshots();
    // The uncaptured topmost placeholder holds the timestamp the next step
    // will be recorded under.
    m_first_unreported_step = snapshots.empty()             ? 0 :
                              snapshots.back().is_topmost() ? snapshots.back().timestamp :
                                                              snapshots.back().timestamp + 1;
    m_seen_active_step = stack.active_snapshot_time();
}

void OrcaWorkspaceAdapter::record_history_edits()
{
    const UndoRedo::Stack&                  stack     = m_plater.undo_redo_stack_main();
    const std::vector<UndoRedo::Snapshot>& snapshots = stack.snapshots();
    bool recorded = false;
    for (const UndoRedo::Snapshot& snapshot : snapshots) {
        if (snapshot.timestamp < m_first_unreported_step || snapshot.is_topmost())
            continue;
        recorded = true;
        // A project separator is a project boundary, reported as one.
        if (snapshot.snapshot_data.snapshot_type != UndoRedo::SnapshotType::ProjectSeparator &&
            UndoRedo::snapshot_modifies_project(snapshot))
            publish_edit({EditKind::Step, EditActor::Person, snapshot.name});
    }
    const std::size_t active = stack.active_snapshot_time();
    if (!recorded && active != m_seen_active_step) {
        // The active position moved without a new step: undo or redo. The
        // steps between the two positions were undone or redone; the first
        // real one names the move.
        const std::size_t from = std::min(active, m_seen_active_step);
        const std::size_t to   = std::max(active, m_seen_active_step);
        const auto named = std::find_if(snapshots.begin(), snapshots.end(), [from, to](const UndoRedo::Snapshot& snapshot) {
            return snapshot.timestamp >= from && snapshot.timestamp < to && !snapshot.is_topmost() &&
                   UndoRedo::snapshot_modifies_project(snapshot);
        });
        if (named != snapshots.end())
            publish_edit({active < m_seen_active_step ? EditKind::Undo : EditKind::Redo, EditActor::Person, named->name});
    }
    remember_history();
}

void OrcaWorkspaceAdapter::record_settings_edits(bool report)
{
    PresetBundle& presets = *wxGetApp().preset_bundle;
    const std::vector<const PresetCollection*> collections{static_cast<const PresetCollection*>(&presets.prints),
                                                           static_cast<const PresetCollection*>(&presets.filaments),
                                                           static_cast<const PresetCollection*>(&presets.printers)};
    std::vector<PresetReading> now;
    for (const PresetCollection* collection : collections)
        now.push_back({collection->get_edited_preset().name,
                       preset_deltas_of(*collection, collection->current_dirty_options())});
    if (report && m_presets.size() == now.size())
        for (std::size_t index = 0; index < now.size(); ++index) {
            if (m_presets[index].name != now[index].name) {
                publish_edit({EditKind::Preset, EditActor::Person, now[index].name});
                continue;
            }
            for (const WorkspaceEdit& edit : setting_edits(m_presets[index].deltas, now[index].deltas, *collections[index]))
                publish_edit(edit);
        }
    m_presets = std::move(now);
}

void OrcaWorkspaceAdapter::publish_change(WorkspaceChangeReasons reasons)
{
    m_changes.merge(reasons);
    WorkspaceChangeDelivery delivery = m_changes.commit(m_session);
    delivery.deliver();
}

void OrcaWorkspaceAdapter::remember_current_ids() const
{
    for (const ModelObject* object : m_plater.model().objects)
        m_known_object_ids.insert(object->id().id);
}

SettingsSearchResult OrcaWorkspaceAdapter::search_settings(const SettingsQuery& query) const
{
    wxASSERT(wxIsMainThread());
    if (!process_settings_available()) {
        SettingsSearchResult result;
        result.error = SettingIssue{"", "workspace_unavailable", "No active FFF process preset."};
        return result;
    }
    return search_setting_definitions(process_definitions(), query);
}

SettingsReadResult OrcaWorkspaceAdapter::read_settings(const std::vector<std::string>& keys) const
{
    wxASSERT(wxIsMainThread());
    SettingsReadResult result;
    if (keys.empty() || keys.size() > 32) {
        result.error = SettingIssue{"", "invalid_arguments", "Read 1 to 32 setting keys."};
        return result;
    }
    if (!process_settings_available()) {
        result.error = SettingIssue{"", "workspace_unavailable", "No active FFF process preset."};
        return result;
    }
    auto& prints = wxGetApp().preset_bundle->prints;
    const auto& config = prints.get_edited_preset().config;
    const auto dirty = prints.current_dirty_options();
    const auto system = prints.current_different_from_parent_options();
    for (const auto& key : keys) {
        if (!has_process_setting(key)) {
            result.unknown_keys.push_back(key);
            result.issues.push_back(missing_process_setting(key));
            continue;
        }
        const auto* value = config.option(key);
        if (!value) throw std::logic_error("Process preset is missing its defined option: " + key);
        result.items.push_back({key, value->serialize(), std::find(dirty.begin(), dirty.end(), key) != dirty.end(),
            std::find(system.begin(), system.end(), key) != system.end(), setting_definition(key)});
    }
    return result;
}

SettingsPreview OrcaWorkspaceAdapter::preview_settings(const SettingsPatch& patch) const
{
    wxASSERT(wxIsMainThread());
    SettingsPreview result;
    if (!process_settings_available()) {
        result.issues.push_back({"", "workspace_unavailable", "No active FFF process preset."});
        return result;
    }
    const auto& preset = wxGetApp().preset_bundle->prints.get_edited_preset();
    const auto& current = preset.config;
    result.process_preset = preset.name;
    if (patch.changes.empty() || patch.changes.size() > 32) {
        result.issues.push_back({"", "invalid_arguments", "A patch must contain 1 to 32 settings."});
        return result;
    }
    DynamicPrintConfig next = current;
    for (const auto& [key, text] : patch.changes) {
        if (!has_process_setting(key)) {
            result.issues.push_back(missing_process_setting(key));
            continue;
        }
        const auto def = setting_definition(key);
        if (!def.writable) {
            result.issues.push_back({key, "unsupported_setting_mutation", "This process setting is read-only."});
            continue;
        }
        const auto invalid = [&result, &def, setting_key = key](std::string message) {
            result.issues.push_back({setting_key, "invalid_setting_value", std::move(message), def.enum_values, {}, def.min, def.max});
        };
        if (!complete_setting_number(text, print_config_def.get(key)->type)) {
            invalid("Expected a complete finite " + def.type + " value.");
            continue;
        }
        try {
            next.set_deserialize_strict(key, text);
        } catch (const BadOptionValueException& error) {
            invalid(error.what());
            continue;
        }
        const auto* option = next.option(key);
        const auto* definition = print_config_def.get(key);
        if (definition->type == coEnum) {
            if (!definition->has_enum_value(option->serialize())) invalid("Value is not in the allowed enum values.");
        } else if (!definition->is_value_valid(definition->type == coInt ?
                   static_cast<double>(next.opt_int(key)) : static_cast<const ConfigOptionFloat*>(option)->value)) {
            invalid("Value is outside the setting's bounds.");
        }
    }
    if (!result.issues.empty()) return result;
    check_process_dialogs(next, result);
    if (!result.issues.empty()) return result;

    const DynamicPrintConfig requested = next;
    predict_process_normalization(next);
    for (const auto& key : requested.diff(next)) {
        result.dependencies.push_back({key, current.option(key)->serialize(), next.option(key)->serialize()});
        result.warnings.push_back({key, "normalized_dependency", "Orca will normalize " + key + " to " + next.option(key)->serialize() + "."});
    }

    auto full = wxGetApp().preset_bundle->full_config();
    full.apply(next);
    FullPrintConfig validation_config;
    validation_config.apply(full, true);
    for (const auto& [key, message] : Slic3r::validate(validation_config)) {
        auto& issues = patch.changes.count(key) ? result.issues : result.warnings;
        issues.push_back({key, "incompatible_settings", message});
    }
    for (const auto& [key, text] : patch.changes) {
        const std::string before = current.option(key)->serialize(), after = next.option(key)->serialize();
        if (before != after) result.changes.push_back({key, before, after});
        else result.warnings.push_back({key, "unchanged", "The setting already has this value."});
    }
    // With no explicit change Tab::load_config does not run its normalizer.
    if (result.changes.empty()) {
        result.dependencies.clear();
        result.warnings.erase(std::remove_if(result.warnings.begin(), result.warnings.end(), [](const auto& issue) {
            return issue.code == "normalized_dependency";
        }), result.warnings.end());
    }
    result.valid = result.issues.empty();
    return result;
}

CommandResult OrcaWorkspaceAdapter::apply_settings(const SettingsPatch& patch, const std::vector<SettingChange>& confirmed,
                                                  SettingsPreview& applied)
{
    wxASSERT(wxIsMainThread());
    if (!process_settings_available())
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "No active FFF process preset.");
    auto transaction = m_plater.project_state_transaction();
    const auto& config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    for (const auto& change : confirmed)
        if (!config.option(change.key) || config.option(change.key)->serialize() != change.before)
            return CommandResult::failure(WorkspaceError::StaleSettings, "A confirmed setting changed. Read and preview again.");
    applied = preview_settings(patch);
    if (!applied.valid)
        return CommandResult::failure(WorkspaceError::InvalidSettings, "The settings patch is invalid.");
    const auto actual = settings_confirmation(applied);
    if (actual.size() != confirmed.size() || !std::equal(actual.begin(), actual.end(), confirmed.begin(),
        [](const auto& a, const auto& b) { return a.key == b.key && a.before == b.before && a.after == b.after; }))
        return CommandResult::failure(WorkspaceError::StaleSettings, "The patch no longer matches the approved preview.");
    if (applied.changes.empty())
        return CommandResult::failure(WorkspaceError::NoChange, "All requested values are unchanged.");
    DynamicPrintConfig diff;
    for (const auto& change : applied.changes)
        diff.set_deserialize_strict(change.key, change.after);
    const DynamicPrintConfig before = config;
    // The existing owner updates the preset, its controls, dirty state and
    // slicing. Never imitate this path with direct writes or notifications.
    wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(diff);
    // Preserve an honest result if a future Orca normalizer introduces a
    // secondary rewrite not yet covered by the prediction audit.
    for (const auto& key : before.diff(config)) {
        const auto has_key = [&key](const auto& changes) {
            return std::any_of(changes.begin(), changes.end(), [&key](const auto& change) { return change.key == key; });
        };
        if (!has_key(applied.changes) && !has_key(applied.dependencies))
            applied.dependencies.push_back({key, before.option(key)->serialize(), config.option(key)->serialize()});
    }
    for (auto* changes : {&applied.changes, &applied.dependencies})
        for (auto& change : *changes) {
            const std::string actual_value = config.option(change.key)->serialize();
            if (actual_value != change.after || changes == &applied.dependencies)
                applied.warnings.push_back({change.key, "normalized", "Orca normalized this setting to " + actual_value + "."});
            change.after = actual_value;
        }
    return CommandResult::success();
}

} // namespace Slic3r::GUI::JusPrin::Workspace
