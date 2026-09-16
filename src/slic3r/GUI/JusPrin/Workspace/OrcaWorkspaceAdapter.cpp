#include "OrcaWorkspaceAdapter.hpp"
#include "ModalAnswers.hpp"
#include "OrcaSettings.hpp"
#include "HostLocale.hpp"

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Measure.hpp"
#include "libslic3r/Orient.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/ObjectDataViewModel.hpp"
#include "slic3r/GUI/Jobs/ArrangeJob.hpp"
#include "slic3r/GUI/Jobs/OrientJob.hpp"
#include "slic3r/GUI/Jobs/Worker.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "libslic3r_version.h"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterDiscovery.hpp"
#include "slic3r/GUI/JusPrin/Shell/SetupCommands.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <wx/thread.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
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
    result.setup.presets_dirty = m_plater.is_presets_dirty();
    result.setup.project_path  = into_u8(m_plater.get_project_filename(".3mf"));
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
    // One background process serves the whole application, so "running" is a
    // property of the workspace; during a slice-all the plate it names moves as
    // the run advances.
    result.slicing.running = m_plater.is_background_process_slicing();
    result.jobs            = m_jobs;
    if (result.slicing.running) {
        if (PartPlate* current = plate_list.get_plate(active_index); current != nullptr) {
            result.slicing.plate   = PlateId(m_session, current->id().id);
            const int percent      = current->get_slicing_percent();
            if (percent >= 0 && percent <= 100)
                result.slicing.percent = percent;
        }
    }
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

namespace {
// A step the person would recognise in Orca's own undo list. Project
// separators mark where the history starts, not an edit.
bool is_history_step(const UndoRedo::Snapshot& snapshot)
{
    return !snapshot.is_topmost() && snapshot.snapshot_data.snapshot_type != UndoRedo::SnapshotType::ProjectSeparator &&
           UndoRedo::snapshot_modifies_project(snapshot);
}
} // namespace

namespace {
// Preset::get_printer_type answers for the edited printer whatever preset it
// is called on; this is the same lookup for the preset given.
std::string printer_model_id(const PresetBundle& presets, const Preset& printer)
{
    const std::string model = printer.config.opt_string("printer_model");
    for (const auto& [name, vendor] : presets.vendors)
        for (const auto& candidate : vendor.models)
            if (candidate.name == model)
                return candidate.model_id;
    return {};
}

std::string plate_label(BedType type)
{
    const ConfigOptionDef* definition = print_config_def.get("curr_bed_type");
    if (definition == nullptr || type <= btDefault || int(type) - 1 >= int(definition->enum_labels.size()))
        return {};
    return definition->enum_labels[int(type) - 1];
}

// The plates a printer offers: every plate, less the ones its model excludes,
// as Sidebar::update_bed_type_list computes them for the selected printer.
std::vector<std::string> supported_plates(const PresetCollection& printers, const Preset& printer)
{
    const VendorProfile::PrinterModel* model = PresetUtils::system_printer_model(printer);
    if (model == nullptr)
        if (const Preset* parent = printers.get_preset_parent(printer))
            model = PresetUtils::system_printer_model(*parent);
    std::vector<std::string> result;
    if (const ConfigOptionDef* definition = print_config_def.get("curr_bed_type"))
        for (const std::string& label : definition->enum_labels)
            if (model == nullptr || std::find(model->not_support_bed_types.begin(), model->not_support_bed_types.end(), label) ==
                                        model->not_support_bed_types.end())
                result.push_back(label);
    return result;
}
} // namespace

std::string OrcaWorkspaceAdapter::current_process_preset() const
{
    const PresetBundle* presets = wxGetApp().preset_bundle;
    return presets != nullptr ? presets->prints.get_edited_preset().name : std::string();
}

PrinterSetupPreview OrcaWorkspaceAdapter::preview_printer_setup(const PrinterSetupRequest& request) const
{
    wxASSERT(wxIsMainThread());
    PrinterSetupPreview result;
    const PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr) {
        result.issues.push_back({"unavailable_operation", "The presets are not loaded yet."});
        return result;
    }
    const auto issue = [&result](std::string code, std::string message) {
        result.issues.push_back({std::move(code), std::move(message)});
    };
    // Const lookups only. PresetBundle::update_compatible would rewrite every
    // preset's flag and may select another preset, so compatibility is asked
    // of the free function against the candidate printer, the way
    // update_compatible_internal asks it.
    const PresetCollection& printers = bundle->printers;
    const Preset*           printer  = &printers.get_edited_preset();
    if (request.printer_preset) {
        const Preset* found = printers.find_preset(*request.printer_preset, false);
        if (found == nullptr || !found->is_visible || found->is_default) {
            issue("unknown_preset", "No installed printer preset is named \"" + *request.printer_preset + "\". Read presets_list.");
            return result;
        }
        printer = found;
    }
    const bool printer_changes = printer->name != printers.get_edited_preset().name;
    const PresetWithVendorProfile printer_profile = printers.get_preset_with_vendor_profile(*printer);
    DynamicPrintConfig extra;
    extra.set_key_value("printer_preset", new ConfigOptionString(printer->name));
    const auto* nozzles = printer->config.option<ConfigOptionFloats>("nozzle_diameter");
    if (nozzles != nullptr)
        extra.set_key_value("num_extruders", new ConfigOptionInt(int(nozzles->values.size())));
    const auto compatible = [&](const PresetCollection& collection, const Preset& preset) {
        return is_compatible_with_printer(collection.get_preset_with_vendor_profile(preset), printer_profile, &extra);
    };
    const std::string printer_label = printer->label(false);

    const PresetCollection& prints = bundle->prints;
    const Preset*           print  = &prints.get_edited_preset();
    if (request.process_preset) {
        const Preset* found = prints.find_preset(*request.process_preset, false);
        if (found == nullptr || !found->is_visible || found->is_default)
            issue("unknown_preset", "No installed process preset is named \"" + *request.process_preset + "\".");
        else if (!compatible(prints, *found))
            issue("incompatible_preset", "\"" + found->name + "\" is not made for " + printer_label + ".");
        else
            print = found;
    } else if (printer_changes && !compatible(prints, *print)) {
        result.substitutions.push_back({"process", print->name, "not made for " + printer_label + "; OrcaSlicer picks another"});
    }
    if (request.process_preset && print->name == *request.process_preset) {
        // The same refusal settings_apply_patch makes: a process that would
        // open one of Orca's correction dialogs is not applied without one.
        SettingsPreview dialogs;
        check_process_dialogs(print->config, dialogs);
        for (const SettingIssue& found : dialogs.issues)
            issue("incompatible_settings", found.message);
    }

    const PresetCollection&  filaments = bundle->filaments;
    std::vector<std::string> filament_names = bundle->filament_presets;
    if (request.filament_presets) {
        if (request.filament_presets->size() > filament_names.size())
            issue("invalid_argument", "This project has " + std::to_string(filament_names.size()) + " filament slots.");
        for (std::size_t slot = 0; slot < request.filament_presets->size() && slot < filament_names.size(); ++slot) {
            const std::string& name  = (*request.filament_presets)[slot];
            const Preset*      found = filaments.find_preset(name, false);
            if (found == nullptr || !found->is_visible || found->is_default)
                issue("unknown_preset", "No installed filament preset is named \"" + name + "\".");
            else if (!compatible(filaments, *found))
                issue("incompatible_preset", "\"" + name + "\" is not made for " + printer_label + ".");
            else
                filament_names[slot] = name;
        }
    }
    if (printer_changes)
        for (std::size_t slot = 0; slot < filament_names.size(); ++slot) {
            const bool requested = request.filament_presets && slot < request.filament_presets->size();
            const Preset* current = filaments.find_preset(filament_names[slot], false);
            if (!requested && current != nullptr && !compatible(filaments, *current))
                result.substitutions.push_back({"filament", filament_names[slot], "not made for " + printer_label + "; OrcaSlicer picks another"});
        }

    const std::vector<std::string> plates  = supported_plates(printers, *printer);
    std::string                    current = configured_printer().plate_type;
    std::string                    plate   = current;
    if (request.plate_type) {
        const auto found = std::find_if(plates.begin(), plates.end(), [&request](const std::string& label) {
            return ascii_lower(label) == ascii_lower(*request.plate_type);
        });
        if (found == plates.end()) {
            std::string offered;
            for (const std::string& label : plates) offered += (offered.empty() ? "" : ", ") + label;
            issue("unsupported_plate", printer_label + " offers these plates: " + offered + ".");
        } else {
            plate = *found;
        }
    } else if (printer_changes && !current.empty() && std::find(plates.begin(), plates.end(), current) == plates.end()) {
        result.substitutions.push_back({"plate", current, printer_label + " does not offer it; OrcaSlicer picks another"});
    }

    // What a switch would throw away. A printer switch can replace any of the
    // three, so every edited one is counted; otherwise only the one switched.
    const auto count_edits = [&result](const PresetCollection& collection, const char* kind) {
        const std::vector<std::string> dirty = collection.current_dirty_options();
        if (!dirty.empty())
            result.unsaved_edits.push_back({kind, collection.get_edited_preset().name, dirty.size()});
    };
    if (printer_changes) {
        count_edits(printers, "printer");
        count_edits(prints, "process");
        count_edits(filaments, "filament");
    } else {
        if (print->name != prints.get_edited_preset().name)
            count_edits(prints, "process");
        if (filament_names != bundle->filament_presets)
            count_edits(filaments, "filament");
    }

    result.resulting.preset = printer_label;
    result.resulting.model  = printer_model_id(*bundle, *printer);
    if (nozzles != nullptr)
        result.resulting.nozzle_diameters = nozzles->values;
    result.resulting.plate_type = plate;
    for (const std::string& name : filament_names) {
        ConfiguredFilament filament{name, {}};
        if (const Preset* preset = filaments.find_preset(name, false))
            if (const auto* types = preset->config.option<ConfigOptionStrings>("filament_type"); types && !types->values.empty())
                filament.material = types->values.front();
        result.resulting.filaments.push_back(std::move(filament));
    }
    result.process_preset = print->name;
    result.valid          = result.issues.empty();
    return result;
}

CommandResult OrcaWorkspaceAdapter::apply_printer_setup(const PrinterSetupRequest& request, PrinterSetupPreview& applied)
{
    wxASSERT(wxIsMainThread());
    const PrinterSetupPreview preview = preview_printer_setup(request);
    if (!preview.valid)
        return CommandResult::failure(WorkspaceError::InvalidArgument, preview.issues.front().message);
    if (!preview.unsaved_edits.empty() && !request.discard_unsaved_edits)
        return CommandResult::failure(WorkspaceError::InvalidArgument, "Unsaved preset edits would be lost.");
    PresetBundle* bundle = wxGetApp().preset_bundle;
    const auto tab = [](Preset::Type type) { return wxGetApp().get_tab(type); };

    // Tab::select_preset asks about unsaved edits in a modal. The caller has
    // already said to drop them, so they are dropped first and nothing asks.
    for (const UnsavedEdits& edits : preview.unsaved_edits) {
        const Preset::Type type = edits.kind == "printer" ? Preset::TYPE_PRINTER :
                                  edits.kind == "process" ? Preset::TYPE_PRINT : Preset::TYPE_FILAMENT;
        if (Tab* owner = tab(type)) {
            owner->get_presets()->discard_current_changes();
            owner->load_current_preset();
        }
    }

    const std::string previous_process = current_process_preset();
    const std::string previous_plate   = configured_printer().plate_type;
    const std::vector<std::string> previous_filaments = bundle->filament_presets;
    {
        const ProjectStateTransaction transaction = m_plater.project_state_transaction();
        if (request.printer_preset && !SetupCommands::select_printer_preset(m_plater, *request.printer_preset))
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer did not select the printer.");
        if (request.plate_type) {
            const ConfigOptionDef* definition = print_config_def.get("curr_bed_type");
            int value = 0;
            for (int index = 0; definition != nullptr && index < int(definition->enum_labels.size()); ++index)
                if (definition->enum_labels[index] == preview.resulting.plate_type)
                    value = index + 1;
            if (value == 0 || !SetupCommands::select_bed_type(m_plater, value))
                return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer did not select the plate.");
        }
        if (request.process_preset && *request.process_preset != current_process_preset()) {
            // The process branch of Plater::priv::on_select_preset.
            tab(Preset::TYPE_PRINT)->select_preset(*request.process_preset);
            m_plater.on_config_change(bundle->full_config());
        }
        if (request.filament_presets)
            for (std::size_t slot = 0; slot < request.filament_presets->size(); ++slot) {
                const std::string& name = (*request.filament_presets)[slot];
                if (slot < bundle->filament_presets.size() && bundle->filament_presets[slot] == name)
                    continue;
                if (slot == 0) {
                    if (!SetupCommands::select_filament_preset(m_plater, name))
                        return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer did not select the filament.");
                    continue;
                }
                // The filament branch of on_select_preset for a later slot,
                // which does not load the slot into the filament tab.
                bundle->set_filament_preset(slot, name);
                m_plater.update_project_dirty_from_presets();
                bundle->export_selections(*wxGetApp().app_config);
                m_plater.sidebar().update_dynamic_filament_list();
                m_plater.on_filament_change(slot);
                m_plater.sidebar().update_presets(Preset::TYPE_FILAMENT);
                m_plater.on_config_change(bundle->full_config());
            }
        m_plater.notify_project_state_changed(ProjectStateChangeReason::Settings);
    }

    // What actually happened, read back rather than predicted.
    applied            = preview;
    applied.resulting  = configured_printer();
    applied.process_preset = current_process_preset();
    applied.substitutions.clear();
    if (!request.process_preset && applied.process_preset != previous_process)
        applied.substitutions.push_back({"process", previous_process, "replaced with " + applied.process_preset});
    if (!request.plate_type && applied.resulting.plate_type != previous_plate)
        applied.substitutions.push_back({"plate", previous_plate, "replaced with " + applied.resulting.plate_type});
    for (std::size_t slot = 0; slot < previous_filaments.size() && slot < bundle->filament_presets.size(); ++slot) {
        const bool requested = request.filament_presets && slot < request.filament_presets->size();
        if (!requested && bundle->filament_presets[slot] != previous_filaments[slot])
            applied.substitutions.push_back({"filament", previous_filaments[slot], "replaced with " + bundle->filament_presets[slot]});
    }
    return CommandResult::success();
}

namespace {
Vec3 vec3(const Vec3d& v) { return {v.x(), v.y(), v.z()}; }

// "f<revision>-<object>-<volume>-<plane>-<feature>": everything needed to find
// the feature again in the same revision.
struct FeatureAddress
{
    std::uint64_t revision{0}, object{0};
    std::size_t   volume{0};
    int           plane{0}, feature{0};
};

std::string feature_handle(const FeatureAddress& address)
{
    return "f" + std::to_string(address.revision) + "-" + std::to_string(address.object) + "-" +
           std::to_string(address.volume) + "-" + std::to_string(address.plane) + "-" + std::to_string(address.feature);
}

std::optional<FeatureAddress> parse_feature_handle(const std::string& text)
{
    FeatureAddress address;
    unsigned long long revision = 0, object = 0, volume = 0;
    int plane = 0, feature = 0;
    char tail = 0;
    if (std::sscanf(text.c_str(), "f%llu-%llu-%llu-%d-%d%c", &revision, &object, &volume, &plane, &feature, &tail) != 5 ||
        plane < 0 || feature < 0)
        return std::nullopt;
    address.revision = revision;
    address.object   = object;
    address.volume   = static_cast<std::size_t>(volume);
    address.plane    = plane;
    address.feature  = feature;
    return address;
}

// Whether a point in the volume's frame lies on one of the plane's triangles.
bool covered_by(const indexed_triangle_set& its, const std::vector<int>& triangles, const Vec3d& point)
{
    for (int index : triangles) {
        const auto& face = its.indices[index];
        const Vec3d a = its.vertices[face[0]].cast<double>(), b = its.vertices[face[1]].cast<double>(),
                    c = its.vertices[face[2]].cast<double>();
        const Vec3d v0 = b - a, v1 = c - a, v2 = point - a;
        const double d00 = v0.dot(v0), d01 = v0.dot(v1), d11 = v1.dot(v1), d20 = v2.dot(v0), d21 = v2.dot(v1);
        const double denominator = d00 * d11 - d01 * d01;
        if (std::abs(denominator) < 1e-12)
            continue;
        const double v = (d11 * d20 - d01 * d21) / denominator, w = (d00 * d21 - d01 * d20) / denominator;
        if (v >= -1e-6 && w >= -1e-6 && v + w <= 1 + 1e-6)
            return true;
    }
    return false;
}

Transform3d world_of(const ModelObject& object, const ModelVolume& volume)
{
    return object.instances.front()->get_matrix() * volume.get_matrix();
}
} // namespace

// Runs an Orca job unchanged and says how it ended. Orca's worker has no
// completion event; finalize runs on the UI thread, and a job dropped from
// the queue by a later replace_job is never finalized, which the destructor
// reports as cancelled.
class ReportingJob final : public Job
{
public:
    using Done = std::function<void(const char*)>;
    ReportingJob(std::unique_ptr<Job> inner, Done done) : m_inner(std::move(inner)), m_done(std::move(done)) {}
    ~ReportingJob() override
    {
        if (!m_reported) m_done("cancelled");
    }
    void process(Ctl& ctl) override { m_inner->process(ctl); }
    void finalize(bool canceled, std::exception_ptr& eptr) override
    {
        m_inner->finalize(canceled, eptr);
        m_reported = true;
        m_done(canceled ? "cancelled" : eptr ? "failed" : "finished");
    }

private:
    std::unique_ptr<Job> m_inner;
    Done                 m_done;
    bool                 m_reported{false};
};

bool OrcaWorkspaceAdapter::start_job(const std::string& handle, const std::string& kind, std::unique_ptr<Job> job)
{
    Worker& worker = m_plater.get_ui_job_worker();
    if (!worker.is_idle())
        return false;
    if (m_jobs.size() == kJobLimit)
        m_jobs.erase(m_jobs.begin());
    m_jobs.push_back({handle, kind, "running", {}});
    std::weak_ptr<bool> alive = m_alive;
    replace_job(worker, std::make_shared<ReportingJob>(std::move(job), [this, alive, handle](const char* state) {
        if (!alive.expired())
            finish_job(handle, state);
    }));
    return true;
}

void OrcaWorkspaceAdapter::finish_job(const std::string& handle, const char* state)
{
    const auto job = std::find_if(m_jobs.begin(), m_jobs.end(), [&](const WorkspaceJob& j) { return j.handle == handle; });
    if (job == m_jobs.end() || job->state != "running")
        return;
    job->state = state;
    if (job->kind == "arrange" && m_arrange_restore) {
        GLCanvas3D::ArrangeSettings& settings = m_plater.canvas3D()->get_arrange_settings();
        settings.distance        = m_arrange_restore->first;
        settings.enable_rotation = m_arrange_restore->second;
        m_arrange_restore.reset();
    }
    if (job->kind == "arrange" && job->state == "finished") {
        // Orca moves what does not fit onto a new plate or off the plates; an
        // instance no plate holds entirely is what did not fit.
        PartPlateList&         plates  = m_plater.get_partplate_list();
        const ModelObjectPtrs& objects = m_plater.model().objects;
        for (std::size_t index = 0; index < objects.size(); ++index)
            for (std::size_t instance = 0; instance < objects[index]->instances.size(); ++instance)
                if (objects[index]->instances[instance]->printable &&
                    plates.find_instance_belongs(static_cast<int>(index), static_cast<int>(instance)) < 0) {
                    job->not_placed.push_back(ObjectId(m_session, objects[index]->id().id));
                    break;
                }
    }
    publish_change(WorkspaceChangeReasons::Plates | WorkspaceChangeReasons::Transform);
}

CommandResult OrcaWorkspaceAdapter::lay_out(const LayoutRequest& request, const std::string& job_handle, LayoutResult& result)
{
    wxASSERT(wxIsMainThread());
    Model&         model  = m_plater.model();
    PartPlateList& plates = m_plater.get_partplate_list();
    const auto plate_index = [&](PlateId id) -> int {
        if (id.session() != m_session)
            return -1;
        for (int index = 0; index < plates.get_plate_count(); ++index)
            if (plates.get_plate(index)->id().id == id.value())
                return index;
        return -1;
    };
    const auto bed_type_of = [](const std::string& label) -> std::optional<BedType> {
        const ConfigOptionDef* definition = print_config_def.get("curr_bed_type");
        for (int index = 0; definition != nullptr && index < int(definition->enum_labels.size()); ++index)
            if (ascii_lower(definition->enum_labels[index]) == ascii_lower(label))
                return BedType(index + 1);
        return std::nullopt;
    };
    const int filaments = int(wxGetApp().preset_bundle->filament_presets.size());

    // Everything is checked before anything moves.
    std::vector<std::size_t> indices;
    for (const LayoutObject& row : request.objects) {
        const auto resolved = resolve(row.id);
        if (!resolved)
            return id_error(row.id);
        const ModelObject& object = *model.objects[resolved->index];
        if (row.plate && plate_index(*row.plate) < 0)
            return CommandResult::failure(WorkspaceError::StaleId, "That plate is not in the open project");
        if (row.extruder && (*row.extruder < 1 || *row.extruder > filaments))
            return CommandResult::failure(WorkspaceError::InvalidArgument,
                                          "This project has " + std::to_string(filaments) + " filaments");
        if (row.quantity && *row.quantity != object.instances.size() && object.is_cut())
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer does not copy a cut object");
        if (row.name && row.name->find_first_not_of(" \t") == std::string::npos)
            return CommandResult::failure(WorkspaceError::InvalidArgument, "An object name cannot be empty");
        indices.push_back(resolved->index);
    }
    for (const LayoutPlate& row : request.plates) {
        if (row.id && plate_index(*row.id) < 0)
            return CommandResult::failure(WorkspaceError::StaleId, "That plate is not in the open project");
        if (row.bed_type && !bed_type_of(*row.bed_type))
            return CommandResult::failure(WorkspaceError::InvalidArgument, "\"" + *row.bed_type + "\" is not a plate type OrcaSlicer knows");
    }
    if (request.arrange && request.arrange->plate && plate_index(*request.arrange->plate) < 0)
        return CommandResult::failure(WorkspaceError::StaleId, "That plate is not in the open project");
    Worker& worker = m_plater.get_ui_job_worker();
    if (!worker.is_idle())
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer is busy with another job. Try again when it finishes.");

    {
        const ProjectStateTransaction transaction = m_plater.project_state_transaction();
        Plater::TakeSnapshot snapshot(&m_plater, "Lay out plates");

        for (const LayoutPlate& row : request.plates) {
            int index = row.id ? plate_index(*row.id) : -1;
            if (!row.id) {
                // The add-plate toolbar action, less its own snapshot.
                if (!m_plater.can_add_plate())
                    return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer cannot add another plate");
                index = plates.create_plate();
                result.added_plates.push_back(PlateId(m_session, plates.get_plate(index)->id().id));
            }
            PartPlate& plate = *plates.get_plate(index);
            if (row.name)
                plate.set_plate_name(*row.name);
            if (row.bed_type)
                plate.set_bed_type(*bed_type_of(*row.bed_type));
        }

        for (std::size_t row_index = 0; row_index < request.objects.size(); ++row_index) {
            const LayoutObject& row    = request.objects[row_index];
            const std::size_t   index  = indices[row_index];
            ModelObject&        object = *model.objects[index];
            if (row.enabled) {
                // ObjectList::toggle_printable_state, for the whole object.
                for (ModelInstance* instance : object.instances)
                    instance->printable = *row.enabled;
                if (ObjectList* list = wxGetApp().obj_list())
                    for (std::size_t instance = 0; instance < object.instances.size(); ++instance)
                        list->update_printable_state(static_cast<int>(index), static_cast<int>(instance));
                m_plater.canvas3D()->update_instance_printable_state_for_objects({index});
            }
            if (row.quantity && *row.quantity != object.instances.size()) {
                // Orca's own instance commands work on the selected object.
                m_plater.select_object(index);
                if (*row.quantity > object.instances.size())
                    m_plater.increase_instances(*row.quantity - object.instances.size());
                else
                    m_plater.decrease_instances(object.instances.size() - *row.quantity);
                if (object.instances.size() != *row.quantity)
                    return CommandResult::failure(WorkspaceError::UnavailableOperation,
                                                  "OrcaSlicer did not change the copies of " + object.name +
                                                      " (a disabled object cannot be copied)");
            }
            if (row.plate) {
                const int from = plates.find_instance_belongs(static_cast<int>(index), 0);
                const int to   = plate_index(*row.plate);
                if (from != to) {
                    Vec3d delta = from >= 0 ? Vec3d(plates.get_plate(to)->get_origin() - plates.get_plate(from)->get_origin()) :
                                              Vec3d(plates.get_plate(to)->get_center_origin() - object.instance_bounding_box(0).center());
                    delta.z() = 0;
                    m_plater.select_object(index);
                    Selection& selection = m_plater.canvas3D()->get_selection();
                    TransformationType relative;
                    relative.set_world();
                    relative.set_relative();
                    selection.setup_cache();
                    selection.translate(delta, relative);
                    m_plater.canvas3D()->do_move("");
                }
            }
            if (row.name)
                m_plater.rename_object(index, *row.name);
            if (row.extruder) {
                object.config.set_key_value("extruder", new ConfigOptionInt(*row.extruder));
                if (ObjectList* list = wxGetApp().obj_list())
                    list->object_config_options_changed({&object, nullptr});
                m_plater.changed_object(static_cast<int>(index));
            }
        }
        m_plater.update();
        m_plater.notify_project_state_changed(ProjectStateChangeReason::Objects | ProjectStateChangeReason::Plates |
                                              ProjectStateChangeReason::Transform);
    }

    if (request.arrange) {
        // Plater::arrange, with the snapshot above and the spacing and
        // rotation asked for, restored when the job ends.
        GLCanvas3D::ArrangeSettings& settings = m_plater.canvas3D()->get_arrange_settings();
        m_arrange_restore = std::make_pair(settings.distance, settings.enable_rotation);
        if (request.arrange->spacing) settings.distance = float(*request.arrange->spacing);
        if (request.arrange->rotation) settings.enable_rotation = *request.arrange->rotation;
        if (request.arrange->plate) {
            m_plater.select_plate(plate_index(*request.arrange->plate));
            m_plater.set_prepare_state(Job::PREPARE_STATE_MENU);
        } else {
            m_plater.set_prepare_state(Job::PREPARE_STATE_DEFAULT);
        }
        if (!start_job(job_handle, "arrange", std::make_unique<ArrangeJob>())) {
            settings.distance        = m_arrange_restore->first;
            settings.enable_rotation = m_arrange_restore->second;
            m_arrange_restore.reset();
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer is busy with another job. Try again when it finishes.");
        }
        result.arranging = true;
    }
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::place_object(ObjectId id, const PlacementRequest& request, const std::string& job_handle,
                                                 PlacementResult& result)
{
    wxASSERT(wxIsMainThread());
    auto resolved = resolve(id);
    if (!resolved)
        return id_error(id);
    Model& model = m_plater.model();
    if (request.instance >= model.objects[resolved->index]->instances.size())
        return CommandResult::failure(WorkspaceError::InvalidArgument, "The object has no such instance");
    if (request.auto_orient && !m_plater.get_ui_job_worker().is_idle())
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer is busy with another job. Try again when it finishes.");
    if (!request.mirror_axis.empty() && model.objects[resolved->index]->is_cut())
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer does not mirror a cut object");

    // The face must still be the one that was read: resolve it before
    // anything moves.
    std::optional<Vec3d> face_normal;
    if (!request.face_down.empty()) {
        const auto address = parse_feature_handle(request.face_down);
        if (!address || address->object != id.value())
            return CommandResult::failure(WorkspaceError::InvalidArgument, "\"" + request.face_down + "\" is not a face of this object");
        if (address->revision != m_changes.revision())
            return CommandResult::failure(WorkspaceError::FeatureExpired,
                                          "The project changed since \"" + request.face_down + "\" was found. Read the features again.");
        const ModelObject& object = *model.objects[resolved->index];
        if (address->volume >= object.volumes.size() || !object.volumes[address->volume]->is_model_part())
            return CommandResult::failure(WorkspaceError::InvalidArgument, "\"" + request.face_down + "\" is not a face of this object");
        const ModelVolume&       volume = *object.volumes[address->volume];
        const Measure::Measuring measuring(volume.mesh().its);
        if (address->plane >= measuring.get_num_of_planes() ||
            address->feature >= static_cast<int>(measuring.get_plane_features(address->plane).size()) ||
            measuring.get_plane_features(address->plane)[address->feature].get_type() != Measure::SurfaceFeatureType::Plane)
            return CommandResult::failure(WorkspaceError::InvalidArgument, "\"" + request.face_down + "\" is not a face of this object");
        const auto [unused, normal, point] = measuring.get_plane_features(address->plane)[address->feature].get_plane();
        // Selection::flattening_rotate takes the normal in the instance's
        // frame: after the volume matrix, before the instance matrix.
        face_normal = (volume.get_matrix().linear().inverse().transpose() * normal).normalized();
    }

    GLCanvas3D& canvas    = *m_plater.canvas3D();
    Selection&  selection = canvas.get_selection();
    const auto  select = [&]() {
        selection.add_instance(static_cast<unsigned int>(resolved->index), static_cast<unsigned int>(request.instance), true);
        if (ObjectList* list = wxGetApp().obj_list())
            list->update_selections();
    };
    TransformationType world_relative;
    world_relative.set_world();
    world_relative.set_relative();
    world_relative.set_joint();

    {
        const ProjectStateTransaction transaction = m_plater.project_state_transaction();
        // One undo step for everything below; the canvas's own snapshots are
        // suppressed inside it.
        Plater::TakeSnapshot snapshot(&m_plater, "Place object");
        select();

        if (!request.units_fix.empty()) {
            // Orca replaces the object with a converted copy at the end of the
            // list; a too-large question on reload is declined.
            ScopedModalAnswers answers([](wxWindow&, const wxString&) { return int(wxID_NO); });
            m_plater.convert_unit(request.units_fix == "inches" ? ConversionType::CONV_FROM_INCH : ConversionType::CONV_FROM_METER);
            resolved = ResolvedObject{model.objects.size() - 1};
            select();
        }
        if (request.scale || request.scale_to) {
            Vec3d factors = Vec3d::Ones();
            if (request.scale) {
                factors = Vec3d((*request.scale)[0], (*request.scale)[1], (*request.scale)[2]);
            } else {
                const double current = model.objects[resolved->index]->instance_bounding_box(request.instance).size()[request.scale_to->first];
                if (current <= 0)
                    return CommandResult::failure(WorkspaceError::UnavailableOperation, "The object has no size along that axis");
                factors = Vec3d::Constant(request.scale_to->second / current);
            }
            selection.setup_cache();
            selection.scale(factors, world_relative);
            canvas.do_scale("");
        }
        if (!request.mirror_axis.empty()) {
            selection.setup_cache();
            selection.mirror(request.mirror_axis == "x" ? X : request.mirror_axis == "y" ? Y : Z, world_relative);
            canvas.do_mirror("");
        }
        if (face_normal) {
            selection.setup_cache();
            selection.flattening_rotate(*face_normal);
            // The flatten gizmo's own snapshot name: do_rotate keeps a
            // sinking object on the bed only for it.
            canvas.do_rotate("Gizmo-Place on Face");
        }
        if (request.rotate) {
            for (int axis = 0; axis < 3; ++axis) {
                const double degrees = (*request.rotate)[axis];
                if (degrees == 0)
                    continue;
                Vec3d rotation = Vec3d::Zero();
                rotation[axis] = degrees * PI / 180.0;
                selection.setup_cache();
                selection.rotate(rotation, world_relative);
                canvas.do_rotate("");
            }
        }
        if (request.position) {
            const Vec3d offset = model.objects[resolved->index]->instances[request.instance]->get_offset();
            selection.setup_cache();
            selection.translate(Vec3d((*request.position)[0] - offset.x(), (*request.position)[1] - offset.y(), 0), world_relative);
            canvas.do_move("");
        }
        if (request.drop_to_bed)
            selection.drop();
        m_plater.notify_project_state_changed(ProjectStateChangeReason::Transform | ProjectStateChangeReason::Objects);
    }

    const ModelObject& placed = *model.objects[resolved->index];
    result.object    = ObjectId(m_session, placed.id().id);
    result.transform = transform_of(*placed.instances[request.instance]);
    result.size      = vec3(placed.instance_bounding_box(request.instance).size());

    if (request.auto_orient) {
        // Plater::orient, with its snapshot already taken above and the job
        // wrapped so its end is reported under the caller's handle. The job
        // orients the selection, which is this object.
        m_plater.set_prepare_state(Job::PREPARE_STATE_DEFAULT);
        if (!start_job(job_handle, "orient", std::make_unique<OrientJob>()))
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer is busy with another job. Try again when it finishes.");
        result.orienting = true;
    }
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::analyze_object(ObjectId id, const AnalysisRequest& request, ObjectAnalysis& result) const
{
    wxASSERT(wxIsMainThread());
    const auto resolved = resolve(id);
    if (!resolved)
        return id_error(id);
    Model&             model  = m_plater.model();
    const ModelObject& object = *model.objects[resolved->index];
    if (object.instances.empty())
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "The object has no instance to measure");
    const std::uint64_t revision = m_changes.revision();

    if (request.mesh) {
        MeshFacts facts;
        const TriangleMeshStats stats = object.get_object_stl_stats();
        facts.size              = vec3(object.instance_bounding_box(0).size());
        facts.volume            = stats.volume;
        // The object-level stats do not add up facets; the parts' meshes do.
        for (const ModelVolume* volume : object.volumes)
            if (volume->is_model_part())
                facts.facets += volume->mesh().facets_count();
        facts.parts             = static_cast<std::size_t>(std::max(stats.number_of_parts, 0));
        facts.open_edges        = stats.open_edges;
        facts.edges_fixed       = stats.repaired_errors.edges_fixed;
        facts.degenerate_facets = stats.repaired_errors.degenerate_facets;
        facts.facets_removed    = stats.repaired_errors.facets_removed;
        facts.facets_reversed   = stats.repaired_errors.facets_reversed;
        facts.backwards_edges   = stats.repaired_errors.backwards_edges;
        for (const ModelVolume* volume : object.volumes) {
            if (facts.part_list.size() == 16)
                break;
            const char* kind = volume->is_model_part()      ? "model" :
                               volume->is_modifier()        ? "modifier" :
                               volume->is_negative_volume() ? "negative" :
                               volume->is_support_enforcer() ? "enforcer" : "blocker";
            facts.part_list.push_back({volume->id().id, volume->name, kind, volume->mesh().facets_count()});
        }
        // Orca's own load-time unit tests work on a whole model; ask them
        // about a model holding only this object, metres first as Orca does.
        Model single;
        single.add_object(object);
        facts.units_suspicion = single.looks_like_saved_in_meters() ? "meters" :
                                single.looks_like_imperial_units()  ? "inches" : "";
        result.mesh = facts;
    }

    if (request.features || request.orientations) {
        ObjectFeatures features;
        for (std::size_t volume_index = 0; volume_index < object.volumes.size(); ++volume_index) {
            const ModelVolume& volume = *object.volumes[volume_index];
            if (!volume.is_model_part())
                continue;
            // The measure gizmo's own construction: one Measuring per part on
            // its untransformed mesh, features moved to the world afterwards.
            const Transform3d            world = world_of(object, volume);
            const indexed_triangle_set&  its   = volume.mesh().its;
            const Measure::Measuring     measuring(its);
            for (int plane = 0; plane < measuring.get_num_of_planes(); ++plane) {
                const std::vector<int>& triangles = measuring.get_plane_triangle_indices(plane);
                double area = 0;
                std::vector<Vec3d> corners;
                for (int index : triangles) {
                    const auto& face = its.indices[index];
                    const Vec3d a = world * its.vertices[face[0]].cast<double>(), b = world * its.vertices[face[1]].cast<double>(),
                                c = world * its.vertices[face[2]].cast<double>();
                    area += 0.5 * (b - a).cross(c - a).norm();
                    corners.insert(corners.end(), {a, b, c});
                }
                const std::vector<Measure::SurfaceFeature>& found = measuring.get_plane_features(plane);
                for (int index = 0; index < static_cast<int>(found.size()); ++index) {
                    Measure::SurfaceFeature feature(found[index]);
                    const FeatureAddress address{revision, id.value(), volume_index, plane, index};
                    if (feature.get_type() == Measure::SurfaceFeatureType::Plane) {
                        feature.translate(world);
                        const auto [unused, normal, point] = feature.get_plane();
                        // A plane whose border could not be walked has no centre.
                        if (!normal.allFinite() || !point.allFinite())
                            continue;
                        // The extent in the face's plane: the tightest box
                        // aligned with one of its edges, so a rectangle
                        // reports its sides rather than a diagonal.
                        const Vec3d unit = normal.normalized();
                        std::array<double, 2> extent{0, 0};
                        double best = std::numeric_limits<double>::max();
                        for (std::size_t edge = 0; edge + 1 < corners.size() && edge < 3 * 64; ++edge) {
                            Vec3d axis = corners[edge + (edge % 3 == 2 ? -2 : 1)] - corners[edge];
                            axis -= unit * unit.dot(axis);
                            if (axis.norm() < 1e-9)
                                continue;
                            axis.normalize();
                            const Vec3d other = unit.cross(axis);
                            double min_u = std::numeric_limits<double>::max(), max_u = -min_u, min_v = min_u, max_v = -min_u;
                            for (const Vec3d& corner : corners) {
                                min_u = std::min(min_u, corner.dot(axis)), max_u = std::max(max_u, corner.dot(axis));
                                min_v = std::min(min_v, corner.dot(other)), max_v = std::max(max_v, corner.dot(other));
                            }
                            if ((max_u - min_u) * (max_v - min_v) < best) {
                                best   = (max_u - min_u) * (max_v - min_v);
                                extent = {std::max(max_u - min_u, max_v - min_v), std::min(max_u - min_u, max_v - min_v)};
                            }
                        }
                        features.faces.push_back({feature_handle(address), area, vec3(unit), vec3(point), extent});
                    } else if (feature.get_type() == Measure::SurfaceFeatureType::Circle) {
                        const auto [local_center, local_radius, local_normal] = feature.get_circle();
                        if (covered_by(its, triangles, local_center))
                            continue; // the rim of a boss or a disc, not a hole
                        feature.translate(world);
                        const auto [center, radius, normal] = feature.get_circle();
                        features.holes.push_back({feature_handle(address), 2 * radius, vec3(center), vec3(normal.normalized())});
                    }
                }
            }
        }
        std::stable_sort(features.faces.begin(), features.faces.end(),
                         [](const FaceFeature& a, const FaceFeature& b) { return a.area > b.area; });
        std::stable_sort(features.holes.begin(), features.holes.end(),
                         [](const HoleFeature& a, const HoleFeature& b) { return a.diameter > b.diameter; });

        if (request.orientations) {
            std::vector<Vec3d> ups;
            for (const OrientationCandidate& candidate : request.candidates) {
                if (candidate.up) {
                    const Vec3d up((*candidate.up)[0], (*candidate.up)[1], (*candidate.up)[2]);
                    if (up.norm() < 1e-9)
                        return CommandResult::failure(WorkspaceError::InvalidArgument, "An up direction cannot be zero");
                    ups.push_back(up);
                    continue;
                }
                const auto address = parse_feature_handle(candidate.face_down);
                if (address && address->object == id.value() && address->revision != revision)
                    return CommandResult::failure(WorkspaceError::FeatureExpired,
                                                  "The project changed since \"" + candidate.face_down + "\" was found. Read the features again.");
                const auto face = std::find_if(features.faces.begin(), features.faces.end(),
                                               [&](const FaceFeature& f) { return f.handle == candidate.face_down; });
                if (face == features.faces.end())
                    return CommandResult::failure(WorkspaceError::InvalidArgument, "\"" + candidate.face_down + "\" is not a face of this object");
                ups.push_back(-Vec3d(face->normal[0], face->normal[1], face->normal[2]));
            }
            // OrientJob's own inputs for one instance: the parts in the
            // instance's rotation, the object's or the print's overhang
            // angle, and the canvas's area-or-volume choice.
            orientation::OrientMesh mesh;
            mesh.name = object.name;
            mesh.mesh = object.raw_mesh();
            mesh.mesh.transform(object.instances.front()->get_matrix_no_offset());
            mesh.overhang_angle = object.config.has("support_threshold_angle") ?
                                      object.config.opt_int("support_threshold_angle") :
                                      wxGetApp().preset_bundle->full_config().opt_int("support_threshold_angle");
            orientation::OrientParams params;
            if (m_plater.canvas3D()->get_orient_settings().min_area) {
                // Exactly as OrientJob::process builds it.
                orientation::OrientParamsArea area;
                std::memcpy(static_cast<void*>(&params), &area, sizeof(params));
                params.min_volume = false;
            } else {
                params.min_volume = true;
            }
            std::vector<OrientationOption> options;
            for (const orientation::OrientationScore& score : orientation::score_orientations(mesh, ups, params)) {
                if (options.size() == kOrientationLimit)
                    break;
                OrientationOption option{vec3(score.up), score.unprintability, score.overhang, score.bottom, {}};
                for (const FaceFeature& face : features.faces)
                    if (option.faces_down.size() < kOrientationLimit &&
                        Vec3d(face.normal[0], face.normal[1], face.normal[2]).dot(score.up) < -0.999)
                        option.faces_down.push_back(face.handle);
                options.push_back(std::move(option));
            }
            result.orientations = std::move(options);
        }

        if (features.faces.size() > kFeatureLimit) features.faces.resize(kFeatureLimit), features.truncated = true;
        if (features.holes.size() > kFeatureLimit) features.holes.resize(kFeatureLimit), features.truncated = true;
        if (request.features)
            result.features = std::move(features);
    }

    if (request.fit) {
        ObjectFit       fit;
        PartPlateList&  plates = m_plater.get_partplate_list();
        const int       object_index = static_cast<int>(resolved->index);
        int             home = -1;
        for (std::size_t instance = 0; instance < object.instances.size(); ++instance) {
            if (fit.instances.size() == 16) { fit.truncated = true; break; }
            InstanceFit row;
            row.instance = instance;
            const int plate = plates.find_instance_belongs(object_index, static_cast<int>(instance));
            if (plate >= 0) {
                row.plate  = PlateId(m_session, plates.get_plate(plate)->id().id);
                row.inside = !plates.get_plate(plate)->check_outside(object_index, static_cast<int>(instance));
                if (home < 0) home = plate;
            }
            fit.instances.push_back(row);
        }
        const Polygons footprint{object.instances.front()->convex_hull_2d()};
        const TriangleMeshStats stats = object.get_object_stl_stats();
        const Vec3d size = object.instance_bounding_box(0).size();
        for (std::size_t other = 0; other < model.objects.size(); ++other) {
            if (other == resolved->index)
                continue;
            ModelObject& candidate = *model.objects[other];
            const ObjectId candidate_id(m_session, candidate.id().id);
            if (home >= 0)
                for (std::size_t instance = 0; instance < candidate.instances.size(); ++instance)
                    if (plates.find_instance(static_cast<int>(other), static_cast<int>(instance)) == home &&
                        !intersection(footprint, Polygons{candidate.instances[instance]->convex_hull_2d()}).empty()) {
                        if (fit.overlaps.size() < 16) fit.overlaps.push_back(candidate_id); else fit.truncated = true;
                        break;
                    }
            if (candidate.instances.empty())
                continue;
            // Same facet count, volume within 0.1 % and size within 0.05 mm:
            // the same model loaded twice, not a second instance.
            const TriangleMeshStats other_stats = candidate.get_object_stl_stats();
            if (other_stats.number_of_facets == stats.number_of_facets &&
                std::abs(other_stats.volume - stats.volume) <= 0.001 * std::abs(stats.volume) &&
                (candidate.instance_bounding_box(0).size() - size).cwiseAbs().maxCoeff() < 0.05) {
                if (fit.likely_duplicates.size() < 16) fit.likely_duplicates.push_back(candidate_id); else fit.truncated = true;
            }
        }
        result.fit = std::move(fit);
    }

    if (request.measure) {
        const auto feature_at = [&](const std::string& handle, std::optional<Measure::SurfaceFeature>& out) -> CommandResult {
            const auto address = parse_feature_handle(handle);
            if (!address || address->object != id.value())
                return CommandResult::failure(WorkspaceError::InvalidArgument, "\"" + handle + "\" is not a feature of this object");
            if (address->revision != revision)
                return CommandResult::failure(WorkspaceError::FeatureExpired,
                                              "The project changed since \"" + handle + "\" was found. Read the features again.");
            if (address->volume >= object.volumes.size() || !object.volumes[address->volume]->is_model_part())
                return CommandResult::failure(WorkspaceError::InvalidArgument, "\"" + handle + "\" is not a feature of this object");
            const ModelVolume&        volume = *object.volumes[address->volume];
            const Measure::Measuring  measuring(volume.mesh().its);
            if (address->plane >= measuring.get_num_of_planes() ||
                address->feature >= static_cast<int>(measuring.get_plane_features(address->plane).size()))
                return CommandResult::failure(WorkspaceError::InvalidArgument, "\"" + handle + "\" is not a feature of this object");
            out.emplace(measuring.get_plane_features(address->plane)[address->feature]);
            out->translate(world_of(object, volume));
            return CommandResult::success();
        };
        std::optional<Measure::SurfaceFeature> from, to;
        if (auto found = feature_at(request.measure->first, from); !found.succeeded()) return found;
        if (auto found = feature_at(request.measure->second, to); !found.succeeded()) return found;
        // As the measure gizmo asks it.
        const Measure::MeasurementResult measured = Measure::get_measurement(*from, *to, true);
        FeatureMeasurement measurement;
        if (measured.distance_strict) measurement.distance = measured.distance_strict->dist;
        if (measured.distance_infinite) measurement.plane_distance = measured.distance_infinite->dist;
        if (measured.angle) measurement.angle = measured.angle->angle * 180.0 / PI;
        if (measured.distance_xyz) measurement.delta = vec3(*measured.distance_xyz);
        result.measurement = measurement;
    }
    return CommandResult::success();
}

std::vector<ObjectDetails> OrcaWorkspaceAdapter::object_details() const
{
    wxASSERT(wxIsMainThread());
    std::vector<ObjectDetails> result;
    PartPlateList&         plates  = m_plater.get_partplate_list();
    const ModelObjectPtrs& objects = m_plater.model().objects;
    for (std::size_t index = 0; index < objects.size(); ++index) {
        const ModelObject& object = *objects[index];
        ObjectDetails row;
        row.id        = ObjectId(m_session, object.id().id);
        row.name      = object.name;
        row.instances = object.instances.size();
        row.printable = object.printable;
        row.overrides = object.config.size();
        if (object.config.has("extruder"))
            row.extruder = object.config.opt_int("extruder");
        for (const ModelVolume* volume : object.volumes) {
            if (volume->is_model_part()) ++row.parts;
            else if (volume->is_modifier()) ++row.modifiers;
            else if (volume->is_negative_volume()) ++row.negative_parts;
            else if (volume->is_support_modifier()) ++row.support_volumes;
        }
        if (!object.instances.empty())
            row.size = vec3(object.instance_bounding_box(0).size());
        for (int plate = 0; plate < plates.get_plate_count(); ++plate)
            for (std::size_t instance = 0; instance < object.instances.size(); ++instance)
                if (plates.get_plate(plate)->contain_instance(static_cast<int>(index), static_cast<int>(instance))) {
                    row.plates.push_back(PlateId(m_session, plates.get_plate(plate)->id().id));
                    break;
                }
        result.push_back(std::move(row));
    }
    return result;
}

ConfiguredPrinter OrcaWorkspaceAdapter::configured_printer() const
{
    wxASSERT(wxIsMainThread());
    ConfiguredPrinter result;
    PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr)
        return result;
    Preset& printer = presets->printers.get_edited_preset();
    result.preset = presets->printers.get_selected_preset().label(false);
    result.model  = printer_model_id(*presets, printer);
    if (const auto* nozzles = printer.config.option<ConfigOptionFloats>("nozzle_diameter"))
        result.nozzle_diameters = nozzles->values;
    // The plate's own choice, falling back to the project's.
    if (const PartPlate* plate = m_plater.get_partplate_list().get_curr_plate()) {
        result.plate_type = plate_label(plate->get_bed_type(true));
    }
    for (const std::string& name : presets->filament_presets) {
        ConfiguredFilament filament{name, {}};
        if (const Preset* preset = presets->filaments.find_preset(name, false))
            if (const auto* types = preset->config.option<ConfigOptionStrings>("filament_type"); types && !types->values.empty())
                filament.material = types->values.front();
        result.filaments.push_back(std::move(filament));
    }
    return result;
}

WorkspaceHistory OrcaWorkspaceAdapter::history() const
{
    const UndoRedo::Stack&                  stack     = m_plater.undo_redo_stack_main();
    const std::vector<UndoRedo::Snapshot>& snapshots = stack.snapshots();
    WorkspaceHistory result;
    result.restorable = m_plater.can_restore_project_history();
    // A snapshot is taken before its edit, so the step is in force once the
    // active position has moved past it.
    for (const UndoRedo::Snapshot& snapshot : snapshots)
        if (is_history_step(snapshot))
            result.steps.push_back({snapshot.timestamp, snapshot.name, snapshot.timestamp < stack.active_snapshot_time()});
    if (result.steps.size() > kHistoryLimit) {
        result.steps.erase(result.steps.begin(), result.steps.end() - kHistoryLimit);
        result.truncated = true;
    }
    return result;
}

CommandResult OrcaWorkspaceAdapter::restore_history(std::uint64_t step, HistoryPoint point)
{
    wxASSERT(wxIsMainThread());
    if (!m_plater.can_restore_project_history())
        return CommandResult::failure(WorkspaceError::UnavailableOperation,
                                      "Another tool is open with its own undo history. Close it first.");
    const std::vector<UndoRedo::Snapshot>& snapshots = m_plater.undo_redo_stack_main().snapshots();
    const auto found = std::find_if(snapshots.begin(), snapshots.end(), [step](const UndoRedo::Snapshot& snapshot) {
        return snapshot.timestamp == step && is_history_step(snapshot);
    });
    if (found == snapshots.end())
        return CommandResult::failure(WorkspaceError::StaleId, "That step is no longer in the history. Read it again.");
    // Before the step is the snapshot taken for it. After it is the next
    // snapshot that changed the project, or the present: the same place
    // Orca's redo lands.
    auto target = found;
    if (point == HistoryPoint::After)
        target = std::find_if(std::next(found), snapshots.end(), [](const UndoRedo::Snapshot& snapshot) {
            return snapshot.is_topmost() || UndoRedo::snapshot_modifies_project(snapshot);
        });
    if (target == snapshots.end())
        return CommandResult::failure(WorkspaceError::StaleId, "That step is no longer in the history. Read it again.");
    if (target->timestamp == m_plater.undo_redo_stack_main().active_snapshot_time())
        return CommandResult::failure(WorkspaceError::NoChange, "The project is already there");
    if (!m_plater.restore_project_history(target->timestamp))
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer did not move the history");
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::start_slice(std::optional<PlateId> plate, bool preempt)
{
    wxASSERT(wxIsMainThread());
    // Nothing in Orca records who started a run, so a slice in flight may be
    // the person's. Taking it over is the caller's decision to state.
    if (m_plater.is_background_process_slicing() && !preempt)
        return CommandResult::failure(WorkspaceError::UnavailableOperation,
                                      "A slice is already running. Wait for it, or ask again with preempt.");

    PartPlateList& plates = m_plater.get_partplate_list();
    if (plate) {
        if (plate->session() != m_session)
            return CommandResult::failure(WorkspaceError::StaleId, "That plate belongs to a project that is no longer open");
        int index = -1;
        for (int candidate = 0; candidate < plates.get_plate_count(); ++candidate) {
            const PartPlate* found = plates.get_plate(candidate);
            if (found != nullptr && found->id().id == plate->value()) {
                index = candidate;
                break;
            }
        }
        if (index < 0)
            return CommandResult::failure(WorkspaceError::InvalidId, "No such plate");
        PartPlate* target = plates.get_plate(index);
        if (target == nullptr || !target->can_slice())
            return CommandResult::failure(WorkspaceError::UnavailableOperation,
                                          "That plate cannot be sliced as it stands");
        // Orca slices the current plate, so selecting it is part of starting
        // the run, exactly as the header's own Slice button does it.
        if (plates.get_curr_plate_index() != index)
            m_plater.select_plate(index);
    }

    // The toolbar events are Orca's own entry points: they carry the slice-all
    // bookkeeping, the Prepare switch, and the auto-preview rule with them.
    // Posting them keeps one owner for slicing rather than a second path.
    m_plater.exit_gizmo();
    m_plater.update(true, true);
    SimpleEvent event(plate ? EVT_GLTOOLBAR_SLICE_PLATE : EVT_GLTOOLBAR_SLICE_ALL);
    m_plater.GetEventHandler()->ProcessEvent(event);
    return CommandResult::success();
}

std::vector<PrinterDevice> OrcaWorkspaceAdapter::printers() const
{
    wxASSERT(wxIsMainThread());
    std::vector<PrinterDevice> result;
    // The fork's own discovery, asked to include what it normally hides:
    // setup offers printers you can connect to now, but "what printers do I
    // have" is a different question and an unreachable one is a real answer.
    for (const PrinterSetup::DiscoveredPrinter& found : PrinterSetup::discover_printers(true)) {
        PrinterDevice device;
        device.id         = found.stable_id;
        device.name       = found.name;
        device.model      = found.device_model_id;
        device.connection = found.connection;
        device.activity   = found.activity == PrinterSetup::PrinterActivity::Printing ? "printing" :
                            found.activity == PrinterSetup::PrinterActivity::Idle     ? "idle" :
                                                                                        "offline";
        // The device reports a float; 0.4f is not 0.4 once widened.
        if (found.nozzle_diameter > 0.)
            device.nozzle_diameter = std::round(found.nozzle_diameter * 100.) / 100.;
        if (found.observed_at_ms != 0)
            device.observed_at_ms = found.observed_at_ms;
        for (const auto& spool : found.spools) {
            device.materials.push_back(spool.name);
            device.material_types.push_back(spool.material);
        }
        device.selected = found.selected;
        if (found.progress_percent >= 0)
            device.progress_percent = found.progress_percent;
        device.job                = found.job;
        device.nozzle_temperature = found.nozzle_temperature;
        device.bed_temperature    = found.bed_temperature;
        result.push_back(std::move(device));
    }
    return result;
}

PresetListResult OrcaWorkspaceAdapter::list_presets(const PresetQuery& query) const
{
    wxASSERT(wxIsMainThread());
    PresetListResult result;
    // Const throughout: the non-const accessors on a collection can select a
    // preset when the current index is out of range, and update_compatible can
    // both rewrite every compatibility flag and change the selection.
    const PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr)
        return result;
    const PresetCollection& collection = query.kind == PresetKind::Printer ? presets->printers :
                                         query.kind == PresetKind::Filament ?
                                             static_cast<const PresetCollection&>(presets->filaments) :
                                             static_cast<const PresetCollection&>(presets->prints);
    // What a project actually prints with is the bundle's per-extruder choice,
    // not the filament tab's selection; for the other kinds they are the same.
    const std::string selected = query.kind == PresetKind::Filament && !presets->filament_presets.empty() ?
                                     presets->filament_presets.front() :
                                     collection.get_selected_preset_name();

    const std::string needle = ascii_lower(query.text);
    std::size_t       skipped = 0, offset = 0;
    if (!query.cursor.empty())
        std::from_chars(query.cursor.data(), query.cursor.data() + query.cursor.size(), offset);
    for (const Preset& preset : collection) {
        // A hidden preset is one this installation does not offer; a default
        // is Orca's placeholder, not something anyone prints with.
        if (!preset.is_visible || preset.is_default)
            continue;
        if (query.compatible_only && !preset.is_compatible)
            continue;
        const std::string label = preset.alias.empty() ? preset.name : preset.alias;
        if (!needle.empty() && ascii_lower(preset.name).find(needle) == std::string::npos &&
            ascii_lower(label).find(needle) == std::string::npos)
            continue;
        ++result.total;
        if (skipped++ < offset)
            continue;
        if (result.items.size() >= query.limit) {
            result.truncated = true;
            continue; // keep counting, so the total is the whole answer
        }
        result.items.push_back({preset.name, label, preset.vendor != nullptr ? preset.vendor->name : std::string(),
                                preset.is_system, preset.name == selected, preset.is_compatible});
    }
    if (result.truncated)
        result.next_cursor = std::to_string(offset + result.items.size());
    return result;
}

SliceReport OrcaWorkspaceAdapter::slice_report(PlateId plate) const
{
    wxASSERT(wxIsMainThread());
    SliceReport report;
    if (plate.session() != m_session)
        return report;
    PartPlateList& plates = m_plater.get_partplate_list();
    PartPlate*     target = nullptr;
    for (int index = 0; index < plates.get_plate_count(); ++index)
        if (PartPlate* candidate = plates.get_plate(index);
            candidate != nullptr && candidate->id().id == plate.value())
            target = candidate;
    // A slice in flight is not this plate's result yet, and an invalidated one
    // is not a report: both answer "no current slice".
    if (target == nullptr || !target->is_slice_result_valid() || m_plater.is_background_process_slicing() ||
        target->get_slice_result() == nullptr)
        return report;

    const GCodeProcessorResult& result     = *target->get_slice_result();
    const auto&                 statistics = result.print_statistics;
    const auto&                 mode = statistics.modes[static_cast<std::size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];
    report.valid                = true;
    report.print_time_seconds   = mode.time > 0.f ? static_cast<std::uint32_t>(mode.time) : 0u;
    report.prepare_time_seconds = mode.prepare_time > 0.f ? static_cast<std::uint32_t>(mode.prepare_time) : 0u;
    report.filament_changes     = statistics.total_filament_changes;
    report.extruder_changes     = statistics.total_extruder_changes;

    const auto volume_of = [](const std::map<std::size_t, double>& volumes, std::size_t extruder) {
        const auto found = volumes.find(extruder);
        return found == volumes.end() ? 0.0 : found->second;
    };
    bool every_filament_priced = true;
    for (const auto& [extruder, volume] : statistics.total_volumes_per_extruder) {
        SliceFilamentUse use;
        use.extruder = extruder;
        // Without a diameter or a density the length and the weight would be
        // invented, so they stay zero and the row says only what is known.
        if (extruder < result.filament_diameters.size() && result.filament_diameters[extruder] > 0.f)
            use.length_mm = volume / (PI * sqr(0.5 * double(result.filament_diameters[extruder])));
        if (extruder < result.filament_densities.size())
            use.grams = volume * result.filament_densities[extruder] * 0.001;
        if (extruder < result.filament_costs.size() && result.filament_costs[extruder] > 0.f) {
            use.cost     = use.grams * result.filament_costs[extruder] * 0.001;
            use.has_cost = true;
        } else {
            every_filament_priced = false;
        }
        use.flushed_mm3 = volume_of(statistics.flush_per_filament, extruder);
        use.tower_mm3   = volume_of(statistics.wipe_tower_volumes_per_extruder, extruder);
        use.support_mm3 = volume_of(statistics.support_volumes_per_extruder, extruder);
        report.total_grams += use.grams;
        report.total_cost += use.cost;
        report.filaments.push_back(use);
    }
    // Money is the cost of the whole print or it is not shown, the same rule
    // the setup card applies to the estimate.
    report.has_cost = every_filament_priced && report.total_cost > 0.0;
    if (!report.has_cost)
        report.total_cost = 0.0;

    // Orca's own plate-level warnings, with its own words for them.
    for (const GCodeProcessorResult::SliceWarning& warning : result.warnings) {
        auto mutable_warning = warning;
        const std::string text = Plater::get_slice_warning_string(mutable_warning).ToUTF8().data();
        if (text.empty())
            continue; // Orca deliberately has no words for this one
        report.findings.push_back({warning.error_code, text, warning.level >= 2, {}});
    }

    // The step warnings, which live on the print and its objects rather than on
    // the result. Only the current ones: invalidating a step leaves its
    // warnings behind, marked stale.
    if (const Print* print = target->fff_print(); print != nullptr) {
        const auto collect = [&report](const PrintStateBase::StateWithWarnings& state, const std::string& object) {
            for (const PrintStateBase::Warning& warning : state.warnings)
                if (warning.current && report.findings.size() < 32)
                    report.findings.push_back({{}, warning.message,
                                               warning.level == PrintStateBase::WarningLevel::CRITICAL, object});
        };
        for (int step = 0; step < psCount; ++step)
            collect(print->step_state_with_warnings(static_cast<PrintStep>(step)), {});
        for (const PrintObject* object : print->objects()) {
            const std::string name = object->model_object() != nullptr ? object->model_object()->name : std::string();
            for (int step = 0; step < posCount; ++step)
                collect(object->step_state_with_warnings(static_cast<PrintObjectStep>(step)), name);
        }
        report.conflict = print->get_conflict_string();
    }

    // The result's own toolpath_outside flag is only ever written by a 3mf, so
    // it would be stale on a plate just sliced. Ask the build volume instead,
    // which is what the canvas does before it draws the same warning.
    BoundingBoxf3 paths;
    for (const GCodeProcessorResult::MoveVertex& move : result.moves)
        if (move.type == EMoveType::Extrude && move.extrusion_role != erCustom && move.width != 0.f && move.height != 0.f)
            paths.merge(move.position.cast<double>());
    if (paths.defined)
        report.toolpath_outside = !m_plater.build_volume().all_paths_inside(result, paths);
    return report;
}

CommandResult OrcaWorkspaceAdapter::save_project(const std::string& file_path)
{
    wxASSERT(wxIsMainThread());
    // The same gates as the File menu's Save: a G-code preview or an exported
    // file is not a project that can be saved.
    if (m_plater.only_gcode_mode() || m_plater.using_exported_file())
        return CommandResult::failure(WorkspaceError::UnavailableOperation,
                                      "The open file is a preview, not a project that can be saved");
    // Save renders plate thumbnails, which crash before the canvas has its GL
    // state; the same guard as export_project_archive.
    const GLCanvas3D* canvas = m_plater.get_view3D_canvas3D();
    if (canvas == nullptr || !canvas->is_initialized())
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "The project cannot be saved until the view is ready");

    const boost::filesystem::path target(file_path);
    if (!target.is_absolute() || boost::algorithm::to_lower_copy(target.extension().string()) != ".3mf")
        return CommandResult::failure(WorkspaceError::InvalidArgument, "Save to an absolute path ending in .3mf");
    boost::system::error_code error;
    if (!boost::filesystem::is_directory(target.parent_path(), error))
        return CommandResult::failure(WorkspaceError::InvalidArgument, "The folder to save into does not exist");
    // Save reports a failed write with a modal dialog. A write that would fail
    // for want of permission fails here instead, where it can be reported.
    {
        const boost::filesystem::path probe = target.parent_path() / (".jusprin-write-check-" + std::to_string(m_session.value()));
        boost::nowide::ofstream out(probe.string(), std::ios::binary | std::ios::trunc);
        const bool writable = out.is_open();
        out.close();
        boost::filesystem::remove(probe, error);
        if (!writable)
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "That folder cannot be written to");
    }

    // Orca's own Save, not a copy of it. Save only asks where to save when the
    // project has no file, so naming the file first is the whole difference:
    // the export, the backup removal, the saved and dirty bookkeeping, and the
    // recent-projects entry all stay upstream's.
    const wxString previous = m_plater.get_project_filename(".3mf");
    m_plater.set_project_filename(from_u8(file_path));
    if (m_plater.save_project(false) != wxID_YES) {
        if (!previous.IsEmpty())
            m_plater.set_project_filename(previous);
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "The project could not be saved");
    }
    return CommandResult::success();
}

ProjectDetails OrcaWorkspaceAdapter::project_details() const
{
    wxASSERT(wxIsMainThread());
    ProjectDetails result;
    const Model& model = m_plater.model();
    // The fields ProjectPanel::on_reload reads.
    if (model.design_info)
        result.designer = model.design_info->Designer;
    if (model.model_info) {
        result.title       = model.model_info->model_name;
        result.description = model.model_info->description;
        result.license     = model.model_info->license;
        result.copyright   = model.model_info->copyright;
        result.origin      = model.model_info->origin;
    }
    if (model.profile_info) {
        result.profile_title       = model.profile_info->ProfileTile;
        result.profile_description = model.profile_info->ProfileDescription;
    }
    // A read-only walk: ProjectPanel::Reload would create the default folders.
    // JusPrin's own state and Orca's thumbnail cache are not attachments.
    const boost::filesystem::path root(auxiliary_data_dir());
    boost::system::error_code error;
    for (boost::filesystem::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        if (!boost::filesystem::is_regular_file(it->path(), error))
            continue;
        const boost::filesystem::path relative = it->path().lexically_relative(root);
        const std::string first = relative.begin()->string();
        if (first == "JusPrin" || first == ".thumbnails")
            continue;
        result.attachments.push_back({relative.generic_string(), first, boost::filesystem::file_size(it->path(), error)});
    }
    std::sort(result.attachments.begin(), result.attachments.end(),
              [](const ProjectAttachment& a, const ProjectAttachment& b) { return a.id < b.id; });
    if (result.attachments.size() > kAttachmentLimit) {
        result.attachments.resize(kAttachmentLimit);
        result.attachments_truncated = true;
    }
    // Asked with saved=false it is a query; saved=true would record a save.
    result.backup_current = m_plater.up_to_date(false, true);
    return result;
}

namespace {
// The questions on Orca's load paths, by the title Orca gives them. Each one
// the request decides is answered from it; anything else gets the answer
// that changes least.
int answer_load_question(wxWindow& dialog, const wxString& title, UnitChoice units, bool scale_oversized, bool discard_unsaved)
{
    if (title == _L("Object too small"))
        return units == UnitChoice::ConvertIfTiny ? wxID_YES : wxID_NO;
    if (title == _L("Object too large"))
        return scale_oversized ? wxID_YES : wxID_NO;
    if (title == wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Save"))
        return discard_unsaved ? wxID_NO : wxID_CANCEL;
    // Orca's message dialogs read yes or ok; its other dialogs (OBJ colours,
    // STEP meshing) read ok, and cancel keeps their defaults.
    return dynamic_cast<MsgDialog*>(&dialog) != nullptr ? wxID_NO : wxID_CANCEL;
}

const char* describe_answer(int answer)
{
    return answer == wxID_YES ? "yes" : answer == wxID_NO ? "no" : answer == wxID_OK ? "ok" : "cancel";
}
} // namespace

CommandResult OrcaWorkspaceAdapter::open_project(const ProjectOpenRequest& request, std::vector<LoadDecision>& decisions)
{
    wxASSERT(wxIsMainThread());
    const boost::filesystem::path path(request.path);
    const std::string extension = boost::algorithm::to_lower_copy(path.extension().string());
    if (!request.new_project) {
        static const std::set<std::string> openable{".3mf", ".stl", ".obj", ".step", ".stp", ".amf", ".drc"};
        boost::system::error_code error;
        if (!path.is_absolute() || !boost::filesystem::is_regular_file(path, error))
            return CommandResult::failure(WorkspaceError::InvalidArgument, "Open an absolute path to a file that exists");
        if (openable.count(extension) == 0)
            return CommandResult::failure(WorkspaceError::InvalidArgument, "OrcaSlicer opens .3mf, .stl, .obj, .step, .amf and .drc files");
    }
    if ((m_plater.is_project_dirty() || m_plater.is_presets_dirty()) && !request.discard_unsaved)
        return CommandResult::failure(WorkspaceError::InvalidArgument, "The open project has unsaved changes");

    // Orca asks about edited presets in UnsavedChangesDialog, whose answer is
    // read from the dialog afterwards; dropping the edits first means it is
    // never asked.
    if (request.discard_unsaved)
        for (Preset::Type type : {Preset::TYPE_PRINTER, Preset::TYPE_PRINT, Preset::TYPE_FILAMENT})
            if (Tab* tab = wxGetApp().get_tab(type); tab != nullptr && tab->get_presets()->current_is_dirty()) {
                tab->get_presets()->discard_current_changes();
                tab->load_current_preset();
            }

    // Every question on the way is answered from the request and reported.
    ScopedModalAnswers answers([&](wxWindow& dialog, const wxString& title) {
        return answer_load_question(dialog, title, request.units, request.scale_oversized, request.discard_unsaved);
    });

    bool loaded = true;
    if (request.new_project) {
        loaded = m_plater.new_project(true, true) != wxID_CANCEL;
    } else if (extension == ".3mf") {
        // "<loadall>" is Orca's own way to open a project with its settings
        // without asking how ("<silence>" would also leave the project
        // without its file name); geometry only is the answer the person
        // would otherwise give in ProjectDropDialog, set for this one load.
        if (request.load_project_settings) {
            m_plater.load_project(from_u8(request.path), "<loadall>");
        } else {
            AppConfig&        config   = *wxGetApp().app_config;
            const std::string previous = config.get(SETTING_PROJECT_LOAD_BEHAVIOUR);
            config.set(SETTING_PROJECT_LOAD_BEHAVIOUR, OPTION_PROJECT_LOAD_BEHAVIOUR_LOAD_GEOMETRY);
            m_plater.load_project(from_u8(request.path), "-");
            config.set(SETTING_PROJECT_LOAD_BEHAVIOUR, previous);
        }
        boost::system::error_code error;
        loaded = boost::filesystem::equivalent(into_path(m_plater.get_project_filename(".3mf")), path, error);
    } else {
        // A model file opens as a new project named after it, the way File >
        // New followed by Import leaves it.
        loaded = m_plater.new_project(true, true, from_u8(path.stem().string())) != wxID_CANCEL;
        if (loaded) {
            LoadStrategy strategy = LoadStrategy::LoadModel;
            if (request.units == UnitChoice::Inches)
                strategy = strategy | LoadStrategy::ImperialUnits;
            loaded = !m_plater.load_files(std::vector<boost::filesystem::path>{path}, strategy).empty();
        }
    }

    for (const ModalRecord& record : answers.records())
        decisions.push_back({record.title, describe_answer(record.answer)});
    if (!loaded)
        return CommandResult::failure(WorkspaceError::UnavailableOperation,
                                      request.new_project ? "OrcaSlicer did not start a new project" :
                                                            "OrcaSlicer did not open that file");
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

CommandResult OrcaWorkspaceAdapter::import_objects(const ImportRequest& request, std::vector<LoadDecision>& decisions,
                                                   std::vector<ObjectId>& added)
{
    wxASSERT(wxIsMainThread());
    const boost::filesystem::path path(request.path);
    boost::system::error_code error;
    if (!path.is_absolute() || !boost::filesystem::is_regular_file(path, error) || boost::filesystem::file_size(path, error) == 0)
        return CommandResult::failure(WorkspaceError::InvalidArgument, "The model file does not exist");
    static const std::set<std::string> importable{".3mf", ".stl", ".obj", ".step", ".stp", ".amf", ".drc"};
    if (importable.count(boost::algorithm::to_lower_copy(path.extension().string())) == 0)
        return CommandResult::failure(WorkspaceError::InvalidArgument, "OrcaSlicer imports .stl, .obj, .step, .amf, .drc and .3mf files");
    PartPlateList& plates = m_plater.get_partplate_list();
    int plate = -1;
    if (request.plate) {
        for (int index = 0; index < plates.get_plate_count(); ++index)
            if (request.plate->session() == m_session && plates.get_plate(index)->id().id == request.plate->value())
                plate = index;
        if (plate < 0)
            return CommandResult::failure(WorkspaceError::StaleId, "That plate is not in the open project");
    }

    std::vector<size_t> indices;
    ScopedModalAnswers answers([&](wxWindow& dialog, const wxString& title) {
        return answer_load_question(dialog, title, request.units, request.scale_oversized, false);
    });
    {
        // One coalesced, undoable change. LoadModel is the additive,
        // geometry-only strategy Import uses; a new object lands on the
        // current plate, so the target plate is selected first.
        ProjectStateTransaction transaction = m_plater.project_state_transaction();
        m_plater.take_snapshot("Import model");
        if (plate >= 0)
            m_plater.select_plate(plate);
        LoadStrategy strategy = LoadStrategy::LoadModel;
        if (request.units == UnitChoice::Inches)
            strategy = strategy | LoadStrategy::ImperialUnits;
        indices = m_plater.load_files(std::vector<boost::filesystem::path>{path}, strategy);
    }
    for (const ModalRecord& record : answers.records())
        decisions.push_back({record.title, describe_answer(record.answer)});
    if (indices.empty())
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer did not import that file");
    for (size_t index : indices) {
        const ObjectId id(m_session, m_plater.model().objects[index]->id().id);
        m_known_object_ids.insert(id.value());
        added.push_back(id);
    }
    return CommandResult::success(added.front());
}

CommandResult OrcaWorkspaceAdapter::delete_items(const std::vector<DeleteItem>& items)
{
    wxASSERT(wxIsMainThread());
    Model&         model  = m_plater.model();
    PartPlateList& plates = m_plater.get_partplate_list();
    std::vector<ItemForDelete> subitems;
    std::vector<std::size_t>   objects;
    std::vector<int>           plate_indices;
    std::map<std::size_t, std::pair<std::size_t, std::size_t>> removing; // object -> (model parts, instances)

    // Everything is checked first. Orca reports some refusals from these
    // paths with an error it shows after the call returns, which no answer
    // can reach, so the same conditions are refused here.
    for (const DeleteItem& item : items) {
        if (item.kind == DeleteItem::Kind::Plate) {
            int index = -1;
            for (int candidate = 0; candidate < plates.get_plate_count(); ++candidate)
                if (item.plate.session() == m_session && plates.get_plate(candidate)->id().id == item.plate.value())
                    index = candidate;
            if (index < 0)
                return CommandResult::failure(WorkspaceError::StaleId, "That plate is not in the open project");
            plate_indices.push_back(index);
            continue;
        }
        const auto resolved = resolve(item.object);
        if (!resolved)
            return id_error(item.object);
        const ModelObject& object = *model.objects[resolved->index];
        if (item.kind == DeleteItem::Kind::Object) {
            objects.push_back(resolved->index);
        } else if (item.kind == DeleteItem::Kind::Part) {
            const auto volume = std::find_if(object.volumes.begin(), object.volumes.end(),
                                             [&](const ModelVolume* v) { return v->id().id == item.part; });
            if (volume == object.volumes.end())
                return CommandResult::failure(WorkspaceError::StaleId, "That part is not in " + object.name);
            if (object.is_cut() && ((*volume)->is_model_part() || (*volume)->is_negative_volume()))
                return CommandResult::failure(WorkspaceError::UnavailableOperation,
                                              "OrcaSlicer does not delete the solid parts of a cut object");
            if ((*volume)->is_model_part())
                ++removing[resolved->index].first;
            subitems.emplace_back(itVolume, int(resolved->index), int(volume - object.volumes.begin()));
        } else {
            if (item.instance >= object.instances.size())
                return CommandResult::failure(WorkspaceError::InvalidArgument, object.name + " has no such copy");
            ++removing[resolved->index].second;
            subitems.emplace_back(itInstance, int(resolved->index), int(item.instance));
        }
    }
    std::sort(objects.begin(), objects.end());
    objects.erase(std::unique(objects.begin(), objects.end()), objects.end());
    // Parts and copies of an object that goes whole need no separate step.
    subitems.erase(std::remove_if(subitems.begin(), subitems.end(), [&](const ItemForDelete& item) {
                       return std::binary_search(objects.begin(), objects.end(), std::size_t(item.obj_idx));
                   }),
                   subitems.end());
    for (const auto& [index, counts] : removing) {
        if (std::binary_search(objects.begin(), objects.end(), index))
            continue;
        const ModelObject& object = *model.objects[index];
        const auto parts = std::count_if(object.volumes.begin(), object.volumes.end(), [](const ModelVolume* v) { return v->is_model_part(); });
        if (counts.first >= std::size_t(parts))
            return CommandResult::failure(WorkspaceError::InvalidArgument,
                                          "That would remove every solid part of " + object.name + "; delete the object instead");
        if (counts.second >= object.instances.size())
            return CommandResult::failure(WorkspaceError::InvalidArgument,
                                          "That would remove every copy of " + object.name + "; delete the object instead");
    }
    std::sort(plate_indices.begin(), plate_indices.end());
    plate_indices.erase(std::unique(plate_indices.begin(), plate_indices.end()), plate_indices.end());
    if (!plate_indices.empty() && plate_indices.size() >= std::size_t(plates.get_plate_count()))
        return CommandResult::failure(WorkspaceError::InvalidArgument, "A project keeps at least one plate");

    {
        const ProjectStateTransaction transaction = m_plater.project_state_transaction();
        Plater::TakeSnapshot snapshot(&m_plater, "Delete items");
        // The card named a cut object's broken correspondence already.
        ScopedModalAnswers answers([](wxWindow&, const wxString& title) {
            return title == _L("Delete object which is a part of cut object") ? int(wxID_YES) : int(wxID_NO);
        });
        if (!subitems.empty()) {
            // The object list deletes in reverse order, so it gets them sorted.
            std::sort(subitems.begin(), subitems.end(), [](const ItemForDelete& a, const ItemForDelete& b) {
                return std::tie(a.obj_idx, a.type, a.sub_obj_idx) < std::tie(b.obj_idx, b.type, b.sub_obj_idx);
            });
            wxGetApp().obj_list()->delete_from_model_and_list(subitems);
        }
        for (auto index = objects.rbegin(); index != objects.rend(); ++index)
            if (!m_plater.delete_object(*index))
                return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer did not delete an object");
        // A deleted plate's objects move to another plate, as in Orca.
        for (auto index = plate_indices.rbegin(); index != plate_indices.rend(); ++index)
            m_plater.delete_plate(*index);
        m_plater.notify_project_state_changed(ProjectStateChangeReason::Objects | ProjectStateChangeReason::Plates);
    }
    return CommandResult::success();
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
        // Jobs belong to the project that started them; the new project's
        // action ids, which name them, start again.
        m_jobs.clear();
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
    }
    remember_current_ids();

    const WorkspaceChangeReasons reasons = workspace_reasons(change.reasons);
    // An undo step alone is no workspace change, and must not
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
