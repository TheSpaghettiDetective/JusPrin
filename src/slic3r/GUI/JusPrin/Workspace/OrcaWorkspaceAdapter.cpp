#include "OrcaWorkspaceAdapter.hpp"

#include "libslic3r/Model.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Selection.hpp"

#include <wx/app.h>
#include <wx/glcanvas.h>

#include <map>
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

WorkspaceObject project_object(const ModelObject& object)
{
    WorkspaceObject projected;
    projected.id   = ObjectId(object.id().id);
    projected.name = object.name;
    projected.instances.reserve(object.instances.size());
    for (const ModelInstance* instance : object.instances)
        projected.instances.emplace_back(transform_of(*instance));
    return projected;
}

bool transforms_equal(const ObjectTransform& lhs, const ObjectTransform& rhs)
{
    return lhs.position == rhs.position && lhs.rotation == rhs.rotation && lhs.scale == rhs.scale;
}

using ContentProjection = std::map<ObjectId, std::pair<std::string, std::set<PlateId>>>;

ContentProjection contents_of(const WorkspaceSnapshot& snapshot)
{
    ContentProjection result;
    for (const WorkspacePlate& plate : snapshot.plates)
        for (const WorkspaceObject& object : plate.objects) {
            auto& entry = result[object.id];
            entry.first = object.name;
            entry.second.insert(plate.id);
        }
    return result;
}

std::map<ObjectId, std::vector<ObjectTransform>> transforms_of(const WorkspaceSnapshot& snapshot)
{
    std::map<ObjectId, std::vector<ObjectTransform>> result;
    for (const WorkspacePlate& plate : snapshot.plates)
        for (const WorkspaceObject& object : plate.objects)
            result.emplace(object.id, object.instances);
    return result;
}

bool common_transforms_changed(const WorkspaceSnapshot& lhs, const WorkspaceSnapshot& rhs)
{
    const auto left  = transforms_of(lhs);
    const auto right = transforms_of(rhs);
    for (const auto& item : left) {
        const auto found = right.find(item.first);
        if (found == right.end())
            continue;
        if (item.second.size() != found->second.size())
            return true;
        for (std::size_t i = 0; i < item.second.size(); ++i)
            if (!transforms_equal(item.second[i], found->second[i]))
                return true;
    }
    return false;
}

bool plates_changed(const WorkspaceSnapshot& lhs, const WorkspaceSnapshot& rhs)
{
    if (lhs.active_plate != rhs.active_plate || lhs.plates.size() != rhs.plates.size())
        return true;
    for (std::size_t i = 0; i < lhs.plates.size(); ++i) {
        const WorkspacePlate& left  = lhs.plates[i];
        const WorkspacePlate& right = rhs.plates[i];
        if (left.id != right.id || left.name != right.name || left.active != right.active)
            return true;
    }
    return false;
}

} // namespace

OrcaWorkspaceAdapter::OrcaWorkspaceAdapter(Plater& plater) : m_plater(plater)
{
    wxGLCanvas* canvas = m_plater.get_view3D_canvas3D()->get_wxglcanvas();
    canvas->Bind(EVT_GLCANVAS_OBJECT_SELECT, &OrcaWorkspaceAdapter::on_selection, this);
    canvas->Bind(EVT_GLCANVAS_INSTANCE_MOVED, &OrcaWorkspaceAdapter::on_transform, this);
    canvas->Bind(EVT_GLCANVAS_INSTANCE_ROTATED, &OrcaWorkspaceAdapter::on_transform, this);
    m_plater.Bind(EVT_GLCANVAS_PLATE_SELECT, &OrcaWorkspaceAdapter::on_plate, this);
    snapshot();
}

OrcaWorkspaceAdapter::~OrcaWorkspaceAdapter()
{
    m_lifetime.reset();
    if (GLCanvas3D* canvas3d = m_plater.get_view3D_canvas3D()) {
        if (wxGLCanvas* canvas = canvas3d->get_wxglcanvas()) {
            canvas->Unbind(EVT_GLCANVAS_OBJECT_SELECT, &OrcaWorkspaceAdapter::on_selection, this);
            canvas->Unbind(EVT_GLCANVAS_INSTANCE_MOVED, &OrcaWorkspaceAdapter::on_transform, this);
            canvas->Unbind(EVT_GLCANVAS_INSTANCE_ROTATED, &OrcaWorkspaceAdapter::on_transform, this);
        }
    }
    m_plater.Unbind(EVT_GLCANVAS_PLATE_SELECT, &OrcaWorkspaceAdapter::on_plate, this);
}

WorkspaceSnapshot OrcaWorkspaceAdapter::snapshot() const
{
    WorkspaceSnapshot result;
    result.revision = m_changes.revision();

    PartPlateList& plate_list = m_plater.get_partplate_list();
    const int active_index    = plate_list.get_curr_plate_index();
    result.plates.reserve(plate_list.get_plate_count());
    for (int index = 0; index < plate_list.get_plate_count(); ++index) {
        PartPlate* plate = plate_list.get_plate(index);
        WorkspacePlate projected_plate;
        projected_plate.id     = PlateId(plate->id().id);
        projected_plate.name   = plate->get_plate_name().empty() ? "Plate " + std::to_string(index + 1) : plate->get_plate_name();
        projected_plate.active = index == active_index;
        for (const ModelObject* object : plate->get_objects_on_this_plate()) {
            projected_plate.objects.emplace_back(project_object(*object));
            m_known_object_ids.insert(ObjectId(object->id().id));
        }
        if (projected_plate.active)
            result.active_plate = projected_plate.id;
        result.plates.emplace_back(std::move(projected_plate));
    }

    for (const ModelObject* object : m_plater.model().objects)
        m_known_object_ids.insert(ObjectId(object->id().id));

    Selection& selection = m_plater.canvas3D()->get_selection();
    if (selection.is_empty()) {
        result.selection_status = SelectionStatus::None;
    } else if (selection.is_single_full_object()) {
        const int index = selection.get_object_idx();
        if (index >= 0 && index < static_cast<int>(m_plater.model().objects.size())) {
            result.selection_status = SelectionStatus::Objects;
            result.selected_objects.emplace_back(m_plater.model().objects[index]->id().id);
        } else {
            result.selection_status = SelectionStatus::Unsupported;
        }
    } else if (selection.is_multiple_full_object()) {
        std::set<ObjectId> selected;
        for (const auto& object_instance : selection.get_selected_object_instances()) {
            const int index = object_instance.first;
            if (index >= 0 && index < static_cast<int>(m_plater.model().objects.size()))
                selected.emplace(m_plater.model().objects[index]->id().id);
        }
        result.selection_status = SelectionStatus::Objects;
        result.selected_objects.assign(selected.begin(), selected.end());
    } else {
        result.selection_status = SelectionStatus::Unsupported;
    }

    result.can_undo = m_plater.can_undo();
    result.can_redo = m_plater.can_redo();
    return result;
}

CommandResult OrcaWorkspaceAdapter::select_object(ObjectId id)
{
    const auto object = resolve(id);
    if (!object)
        return missing_result(id);

    Selection& selection = m_plater.canvas3D()->get_selection();
    selection.add_object(static_cast<unsigned int>(object->index));
    wxPostEvent(m_plater.get_view3D_canvas3D()->get_wxglcanvas(), SimpleEvent(EVT_GLCANVAS_OBJECT_SELECT));
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::rename_object(ObjectId id, const std::string& name)
{
    const auto object = resolve(id);
    if (!object)
        return missing_result(id);
    if (name.empty())
        return CommandResult::failure(WorkspaceError::InvalidArgument, "Object name cannot be empty");
    if (!m_plater.rename_object(object->index, name))
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "Object could not be renamed");

    schedule_change(WorkspaceChangeReasons::Contents);
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::duplicate_object(ObjectId id)
{
    const auto object = resolve(id);
    if (!object)
        return missing_result(id);

    const int new_index = m_plater.duplicate_object(object->index);
    if (new_index < 0 || new_index >= static_cast<int>(m_plater.model().objects.size()))
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "Object could not be duplicated");

    const ObjectId new_id(m_plater.model().objects[new_index]->id().id);
    m_known_object_ids.insert(new_id);
    schedule_change(WorkspaceChangeReasons::Contents);
    return CommandResult::success(new_id);
}

CommandResult OrcaWorkspaceAdapter::remove_object(ObjectId id)
{
    const auto object = resolve(id);
    if (!object)
        return missing_result(id);
    if (!m_plater.delete_object(object->index))
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "Object could not be removed");

    schedule_change(WorkspaceChangeReasons::Contents | WorkspaceChangeReasons::Selection);
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::undo()
{
    if (!m_plater.can_undo())
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "Nothing to undo");
    const WorkspaceSnapshot before = snapshot();
    m_plater.undo();
    const WorkspaceSnapshot after = snapshot();

    // Plater::undo() returns void and leaves the model untouched when it finds
    // no project-modifying snapshot to walk back to, so whether the undo landed
    // has to be judged from the state it left behind. History on its own means
    // nothing in the projection moved.
    const WorkspaceChangeReasons reasons = changes_after_history(before, after);
    if (reasons == WorkspaceChangeReasons::History)
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "Undo did not change the workspace");

    schedule_change(reasons);
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::redo()
{
    if (!m_plater.can_redo())
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "Nothing to redo");
    const WorkspaceSnapshot before = snapshot();
    m_plater.redo();
    const WorkspaceSnapshot after = snapshot();

    // Plater::redo() is void and can silently do nothing for the same reason
    // Plater::undo() can, so the result is judged from the state it left behind.
    const WorkspaceChangeReasons reasons = changes_after_history(before, after);
    if (reasons == WorkspaceChangeReasons::History)
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "Redo did not change the workspace");

    schedule_change(reasons);
    return CommandResult::success();
}

WorkspaceSubscription OrcaWorkspaceAdapter::subscribe(WorkspaceChangedCallback callback)
{
    return m_changes.subscribe(std::move(callback));
}

std::optional<OrcaWorkspaceAdapter::ResolvedObject> OrcaWorkspaceAdapter::resolve(ObjectId id) const
{
    if (!id)
        return std::nullopt;
    const ModelObjectPtrs& objects = m_plater.model().objects;
    for (std::size_t index = 0; index < objects.size(); ++index)
        if (objects[index]->id().id == id.value())
            return ResolvedObject{index};
    return std::nullopt;
}

CommandResult OrcaWorkspaceAdapter::missing_result(ObjectId id) const
{
    if (!id)
        return CommandResult::failure(WorkspaceError::InvalidId, "Object ID is invalid");
    const WorkspaceError error = m_known_object_ids.count(id) > 0 ? WorkspaceError::StaleId : WorkspaceError::MissingObject;
    return CommandResult::failure(error, "Object does not exist in the current workspace");
}

void OrcaWorkspaceAdapter::schedule_change(WorkspaceChangeReasons reasons)
{
    m_changes.merge(reasons);
    if (m_dispatch_scheduled)
        return;
    m_dispatch_scheduled = true;

    std::weak_ptr<int> lifetime = m_lifetime;
    wxTheApp->CallAfter([this, lifetime]() {
        if (!lifetime.lock())
            return;
        m_dispatch_scheduled = false;
        m_changes.flush();
    });
}

WorkspaceChangeReasons OrcaWorkspaceAdapter::changes_after_history(const WorkspaceSnapshot& before, const WorkspaceSnapshot& after) const
{
    WorkspaceChangeReasons reasons = WorkspaceChangeReasons::History;
    if (contents_of(before) != contents_of(after))
        reasons |= WorkspaceChangeReasons::Contents;
    if (common_transforms_changed(before, after))
        reasons |= WorkspaceChangeReasons::Transform;
    if (before.selection_status != after.selection_status || before.selected_objects != after.selected_objects)
        reasons |= WorkspaceChangeReasons::Selection;
    if (plates_changed(before, after))
        reasons |= WorkspaceChangeReasons::Plates;
    return reasons;
}

void OrcaWorkspaceAdapter::on_selection(SimpleEvent& event)
{
    schedule_change(WorkspaceChangeReasons::Selection);
    event.Skip();
}

void OrcaWorkspaceAdapter::on_transform(SimpleEvent& event)
{
    schedule_change(WorkspaceChangeReasons::Transform);
    event.Skip();
}

void OrcaWorkspaceAdapter::on_plate(SimpleEvent& event)
{
    schedule_change(WorkspaceChangeReasons::Plates);
    event.Skip();
}

} // namespace Slic3r::GUI::JusPrin::Workspace
