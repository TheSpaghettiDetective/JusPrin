#include "OrcaWorkspaceAdapter.hpp"
#include "OrcaSettings.hpp"

#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Selection.hpp"

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

} // namespace

OrcaWorkspaceAdapter::OrcaWorkspaceAdapter(Plater& plater) : m_plater(plater)
{
    wxASSERT(wxIsMainThread());
    m_session = ProjectSessionId(m_plater.project_state_session());
    m_project_subscription = m_plater.subscribe_project_state(
        [this](const ProjectStateChanged& change) { on_project_state_changed(change); });
    remember_current_ids();
}

OrcaWorkspaceAdapter::~OrcaWorkspaceAdapter()
{
    wxASSERT(wxIsMainThread());
    m_project_subscription.reset();
}

WorkspaceSnapshot OrcaWorkspaceAdapter::snapshot() const
{
    wxASSERT(wxIsMainThread());
    WorkspaceSnapshot result;
    result.session  = m_session;
    result.revision = m_changes.revision();

    result.setup.project_name  = m_plater.get_project_name().ToUTF8().data();
    result.setup.project_dirty = m_plater.is_project_dirty();
    if (const PresetBundle* presets = wxGetApp().preset_bundle; presets != nullptr) {
        result.setup.printer_preset = presets->printers.get_selected_preset().label(false);
        if (presets->printers.get_edited_preset().printer_technology() == ptFFF) {
            result.setup.process_preset = presets->prints.get_edited_preset().name;
            result.setup.process_preset_dirty = !presets->prints.current_dirty_options().empty();
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
        if (projected_plate.sliced && !m_plater.is_background_process_slicing() && plate->get_slice_result())
            projected_plate.slice_result_id = plate->get_slice_result()->id;
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
    // SkipAuxiliary keeps consumer files (and earlier checkpoints) out of the
    // archive; the remaining strategy matches an ordinary project save.
    const SaveStrategy strategy = SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::ShareMesh |
                                  SaveStrategy::SkipAuxiliary;
    if (m_plater.export_3mf(boost::filesystem::path(file_path), strategy) < 0)
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "The project archive could not be written");
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::restore_project_archive(const std::string& file_path)
{
    wxASSERT(wxIsMainThread());
    boost::system::error_code ec;
    if (!boost::filesystem::is_regular_file(file_path, ec) || boost::filesystem::file_size(file_path, ec) == 0)
        return CommandResult::failure(WorkspaceError::InvalidArgument, "The project archive does not exist");

    // The core of Plater::load_project without its dialogs or filename
    // bookkeeping: one coalesced project-replacement event, the stock reset
    // and load paths, and no undo history reaching back across the boundary.
    ProjectStateTransaction transaction = m_plater.project_state_transaction();
    m_plater.reset(false);
    const std::vector<boost::filesystem::path> paths{boost::filesystem::path(file_path)};
    const std::vector<size_t> loaded =
        m_plater.load_files(paths, LoadStrategy::LoadModel | LoadStrategy::LoadConfig | LoadStrategy::Silence);
    m_plater.clear_undo_redo_stack_main();
    if (loaded.empty())
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "The project archive could not be loaded");
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
        // objects to the current project rather than replacing it. (Transaction
        // nesting is supported; restore_project_archive relies on it too.)
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
    if (change.project_replaced) {
        m_session = ProjectSessionId(change.project_session);
        m_known_object_ids.clear();
    }
    remember_current_ids();
    publish_change(workspace_reasons(change.reasons));
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
