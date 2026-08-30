#include "OrcaWorkspaceAdapter.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Selection.hpp"

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

} // namespace Slic3r::GUI::JusPrin::Workspace
