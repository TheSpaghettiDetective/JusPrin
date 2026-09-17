// Reshaping one object through Orca's own operations: a plane cut (the Cut
// class the cut gizmo uses), a split into shells (Plater's split to objects,
// or the object list's split to parts), a merge (the object list's assemble),
// and a CGAL mesh repair (the repair command's per-part step).

#include "OrcaWorkspaceAdapter.hpp"
#include "OrcaGeometry.hpp"

#include "libslic3r/CutUtils.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/MeshBoolean.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosManager.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/AppConfig.hpp"

#include <wx/thread.h>

#include <algorithm>
#include <cmath>

namespace Slic3r::GUI::JusPrin::Workspace {
namespace {

// The support threshold the process uses: 0 means Orca's own 30 degrees.
double support_threshold_degrees()
{
    const auto& config    = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    const int   threshold = config.opt_int("support_threshold_angle");
    return threshold > 0 ? threshold : 30;
}

// One object's pieces as they would lie: its model parts in the world frame
// of its first instance, dropped to the bed.
DividedPiece piece_of(const ModelObject& object)
{
    DividedPiece piece;
    piece.name = object.name;
    if (object.instances.empty())
        return piece;
    const Transform3d instance = object.instances.front()->get_matrix();
    BoundingBoxf3     box;
    std::vector<std::array<Vec3d, 3>> triangles;
    for (const ModelVolume* volume : object.volumes) {
        if (!volume->is_model_part())
            continue;
        const Transform3d            world = instance * volume->get_matrix();
        const indexed_triangle_set&  its   = volume->mesh().its;
        piece.volume += std::abs(volume->mesh().stats().volume * world.linear().determinant());
        for (const auto& face : its.indices) {
            std::array<Vec3d, 3> corners;
            for (int k = 0; k < 3; ++k) {
                corners[k] = world * its.vertices[face[k]].cast<double>();
                box.merge(corners[k]);
            }
            triangles.push_back(corners);
        }
    }
    if (!box.defined)
        return piece;
    piece.size   = vec3(box.size());
    piece.center = vec3(box.center());
    const double limit = std::cos(support_threshold_degrees() * PI / 180.0);
    for (const auto& corners : triangles) {
        Vec3d normal = (corners[1] - corners[0]).cross(corners[2] - corners[0]);
        const double twice_area = normal.norm();
        if (twice_area <= 0)
            continue;
        normal /= twice_area;
        const double lowest = std::min({corners[0].z(), corners[1].z(), corners[2].z()});
        // The faces on the bed need no support.
        if (-normal.z() >= limit && lowest > box.min.z() + 0.1)
            piece.overhang_area += 0.5 * twice_area;
    }
    return piece;
}

Transform3d cut_matrix(const ModelObject& object, const DivideRequest& request)
{
    Vec3d normal(request.normal[0], request.normal[1], request.normal[2]);
    normal.normalize();
    const Vec3d point(request.point[0], request.point[1], request.point[2]);
    Transform3d rotation = Transform3d::Identity();
    rotation.linear()    = Eigen::Quaterniond::FromTwoVectors(Vec3d::UnitZ(), normal).toRotationMatrix();
    return Geometry::translation_transform(point - object.instances.front()->get_offset()) * rotation;
}

ModelObjectCutAttributes cut_attributes(const DivideRequest& request)
{
    ModelObjectCutAttributes attributes = ModelObjectCutAttribute::InvalidateCutInfo;
    if (request.keep_upper)
        attributes = attributes | ModelObjectCutAttribute::KeepUpper;
    if (request.keep_lower)
        attributes = attributes | ModelObjectCutAttribute::KeepLower;
    if (request.as_parts)
        attributes = attributes | ModelObjectCutAttribute::KeepAsParts;
    if (wxGetApp().app_config->get_bool("keep_painting"))
        attributes = attributes | ModelObjectCutAttribute::KeepPaint;
    return attributes;
}

std::vector<ModelVolume*> parts_of(ModelObject& object)
{
    std::vector<ModelVolume*> parts;
    for (ModelVolume* volume : object.volumes)
        if (volume->is_model_part())
            parts.push_back(volume);
    return parts;
}

// One part on its own, placed as its object's first instance.
DividedPiece part_piece(const ModelObject& object, const ModelVolume& part)
{
    Model        scratch;
    ModelObject* single = scratch.add_object();
    single->name        = part.name;
    single->add_instance(*object.instances.front());
    single->add_volume(part);
    return piece_of(*single);
}

std::size_t filament_count()
{
    return std::max<std::size_t>(1, wxGetApp().preset_bundle->filament_presets.size());
}

// A cut needs a plane through the object and a side to keep; a split needs
// more than one shell. Checked before anything is copied or changed.
CommandResult check_divide(const ModelObject& object, const DivideRequest& request)
{
    if (object.instances.empty())
        return CommandResult::failure(WorkspaceError::UnavailableOperation, object.name + " has no instance to divide");
    if (object.is_cut())
        return CommandResult::failure(WorkspaceError::UnavailableOperation,
                                      object.name + " was cut with connectors; divide it with OrcaSlicer's cut tool");
    if (request.mode == DivideRequest::Mode::Plane) {
        const Vec3d normal(request.normal[0], request.normal[1], request.normal[2]);
        if (normal.norm() < 1e-9)
            return CommandResult::failure(WorkspaceError::InvalidArgument, "A cut plane needs a normal");
        if (!request.keep_upper && !request.keep_lower)
            return CommandResult::failure(WorkspaceError::InvalidArgument, "A cut keeps the upper piece, the lower one, or both");
        const BoundingBoxf3 box = object.instance_bounding_box(0);
        const Vec3d unit = normal.normalized();
        const Vec3d point(request.point[0], request.point[1], request.point[2]);
        double below = 0, above = 0;
        for (int corner = 0; corner < 8; ++corner) {
            const Vec3d at((corner & 1) ? box.max.x() : box.min.x(), (corner & 2) ? box.max.y() : box.min.y(),
                           (corner & 4) ? box.max.z() : box.min.z());
            const double side = unit.dot(at - point);
            below = std::min(below, side);
            above = std::max(above, side);
        }
        if (below >= -1e-6 || above <= 1e-6)
            return CommandResult::failure(WorkspaceError::InvalidArgument, "That plane does not pass through " + object.name);
        return CommandResult::success();
    }
    std::size_t parts = 0;
    bool        splittable = false;
    for (const ModelVolume* volume : object.volumes)
        if (volume->is_model_part()) {
            ++parts;
            splittable = splittable || volume->is_splittable();
        }
    // Split to objects also separates the parts of a multi-part object.
    if (!splittable && !(parts > 1 && !request.as_parts))
        return CommandResult::failure(WorkspaceError::InvalidArgument, object.name + " is one shell and cannot be split");
    return CommandResult::success();
}

} // namespace

CommandResult OrcaWorkspaceAdapter::preview_divide(ObjectId id, const DivideRequest& request, DivideResult& result) const
{
    wxASSERT(wxIsMainThread());
    const auto resolved = resolve(id);
    if (!resolved)
        return id_error(id);
    const ModelObject& object = *m_plater.model().objects[resolved->index];
    if (CommandResult checked = check_divide(object, request); !checked.succeeded())
        return checked;
    result = {};
    result.overhang_area_before = piece_of(object).overhang_area;
    // Everything below works on copies in models of its own; the live model,
    // its undo stack and its dirty state are not touched.
    if (request.mode == DivideRequest::Mode::Plane) {
        Cut cut(&object, 0, cut_matrix(object, request), cut_attributes(request));
        for (const ModelObject* piece : cut.perform_with_plane())
            result.pieces.push_back(piece_of(*piece));
    } else {
        Model        scratch;
        ModelObject* copy = scratch.add_object(object);
        if (request.as_parts) {
            for (ModelVolume* part : parts_of(*copy))
                if (part->is_splittable())
                    part->split(static_cast<unsigned>(filament_count()), false);
            for (ModelVolume* part : parts_of(*copy))
                result.pieces.push_back(part_piece(*copy, *part));
        } else {
            ModelObjectPtrs pieces;
            copy->split(&pieces, false);
            for (const ModelObject* piece : pieces)
                result.pieces.push_back(piece_of(*piece));
        }
    }
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::divide_object(ObjectId id, const DivideRequest& request, DivideResult& result)
{
    wxASSERT(wxIsMainThread());
    const auto resolved = resolve(id);
    if (!resolved)
        return id_error(id);
    Model&       model  = m_plater.model();
    ModelObject& object = *model.objects[resolved->index];
    if (CommandResult checked = check_divide(object, request); !checked.succeeded())
        return checked;
    result = {};
    result.overhang_area_before = piece_of(object).overhang_area;
    m_plater.canvas3D()->get_gizmos_manager().reset_all_states();
    const ProjectStateTransaction transaction = m_plater.project_state_transaction();
    const std::size_t             count_before = model.objects.size();
    std::size_t                   added        = 0;
    if (request.mode == DivideRequest::Mode::Plane) {
        // GLGizmoCut3D::perform_cut without connectors: its snapshot, the
        // Cut class, and Plater's own replacement of the object.
        Plater::TakeSnapshot snapshot(&m_plater, "Cut by Plane");
        Cut                  cut(&object, 0, cut_matrix(object, request), cut_attributes(request));
        const ModelObjectPtrs& pieces = cut.perform_with_plane();
        if (pieces.empty())
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer's cut produced nothing");
        added = pieces.size();
        m_plater.apply_cut_object_to_model(resolved->index, pieces);
    } else if (!request.as_parts) {
        // Plater's split to objects, which works on the selected object;
        // without auto-drop it asks nothing. (Its index overload is declared
        // upstream but never defined.)
        m_plater.select_object(resolved->index);
        m_plater.split_object(false);
        if (model.objects.size() < count_before)
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer did not split " + object.name);
        added = model.objects.size() - count_before + 1;
    } else {
        // ObjectList::split for every part, less its selection and message.
        Plater::TakeSnapshot snapshot(&m_plater, "Split to parts");
        const bool keep_paint = wxGetApp().app_config->get_bool("keep_painting");
        for (ModelVolume* part : parts_of(object))
            if (part->is_splittable())
                part->split(static_cast<unsigned>(filament_count()), keep_paint);
        if (ObjectList* list = wxGetApp().obj_list()) {
            list->add_volumes_to_object_in_list(resolved->index);
            list->update_info_items(resolved->index);
        }
        m_plater.changed_object(static_cast<int>(resolved->index));
        m_plater.notify_project_state_changed(ProjectStateChangeReason::Objects);
        for (ModelVolume* part : parts_of(object)) {
            DividedPiece piece = part_piece(object, *part);
            piece.object       = ObjectId(m_session, object.id().id);
            result.pieces.push_back(std::move(piece));
        }
        return CommandResult::success();
    }
    // The new objects are the last ones in the model.
    for (std::size_t index = model.objects.size() - added; index < model.objects.size(); ++index) {
        DividedPiece piece = piece_of(*model.objects[index]);
        piece.object       = ObjectId(m_session, model.objects[index]->id().id);
        result.pieces.push_back(std::move(piece));
    }
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::merge_objects(const std::vector<ObjectId>& ids, ObjectId& merged)
{
    wxASSERT(wxIsMainThread());
    if (ids.size() < 2 || ids.size() > 16)
        return CommandResult::failure(WorkspaceError::InvalidArgument, "Merge 2 to 16 objects");
    Model& model = m_plater.model();
    std::vector<ObjectVolumeID> selection;
    for (ObjectId id : ids) {
        const auto resolved = resolve(id);
        if (!resolved)
            return id_error(id);
        ModelObject* object = model.objects[resolved->index];
        if (object->is_cut())
            return CommandResult::failure(WorkspaceError::UnavailableOperation, object->name + " was cut with connectors and cannot be merged");
        if (std::any_of(selection.begin(), selection.end(), [object](const ObjectVolumeID& s) { return s.object == object; }))
            return CommandResult::failure(WorkspaceError::InvalidArgument, object->name + " is named twice");
        selection.push_back({object, nullptr});
    }
    ObjectList* list = wxGetApp().obj_list();
    if (list == nullptr)
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "The object list is not available");
    m_plater.canvas3D()->get_gizmos_manager().reset_all_states();
    const ProjectStateTransaction transaction = m_plater.project_state_transaction();
    // The object list's assemble works on its selection and takes its own
    // snapshot; select exactly these objects first.
    list->select_items(selection);
    if (!list->can_merge_to_multipart_object())
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer cannot merge these objects");
    const std::size_t count_before = model.objects.size();
    list->merge(true);
    if (model.objects.size() != count_before - ids.size() + 1)
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer did not merge the objects");
    merged = ObjectId(m_session, model.objects.back()->id().id);
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::repair_object(ObjectId id, RepairResult& result)
{
    wxASSERT(wxIsMainThread());
    const auto resolved = resolve(id);
    if (!resolved)
        return id_error(id);
    ModelObject& object = *m_plater.model().objects[resolved->index];
    result = {};
    const auto measure = [&object](std::size_t& open_edges, std::size_t& facets, std::size_t& parts, double& volume) {
        const TriangleMeshStats stats = object.get_object_stl_stats();
        open_edges = static_cast<std::size_t>(std::max(stats.open_edges, 0));
        parts      = static_cast<std::size_t>(std::max(stats.number_of_parts, 0));
        volume     = stats.volume;
        facets     = 0;
        for (const ModelVolume* part : object.volumes)
            if (part->is_model_part())
                facets += part->mesh().facets_count();
    };
    measure(result.open_edges_before, result.facets_before, result.parts_before, result.volume_before);
    std::vector<ModelVolume*> broken;
    for (ModelVolume* part : parts_of(object))
        if (its_num_open_edges(part->mesh().its) != 0)
            broken.push_back(part);
    if (broken.empty()) {
        measure(result.open_edges_after, result.facets_after, result.parts_after, result.volume_after);
        return CommandResult::success();
    }
    m_plater.canvas3D()->get_gizmos_manager().reset_all_states();
    const ProjectStateTransaction transaction = m_plater.project_state_transaction();
    Plater::TakeSnapshot          snapshot(&m_plater, "Repairing model object");
    // The repair command's own step per part, less its progress dialog. The
    // mesh changes, so paint that was keyed to its facets goes, as Orca's
    // repair does when it does not remap paint.
    m_plater.clear_before_change_mesh(static_cast<int>(resolved->index));
    for (ModelVolume* part : broken) {
        TriangleMesh mesh = part->mesh();
        std::string  error;
        if (!MeshBoolean::cgal::repair(mesh, nullptr, &error))
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer could not repair " + object.name + ": " + error);
        part->set_mesh(std::move(mesh));
        part->calculate_convex_hull();
        part->invalidate_convex_hull_2d();
        part->set_new_unique_id();
    }
    object.invalidate_bounding_box();
    object.ensure_on_bed();
    m_plater.changed_mesh(static_cast<int>(resolved->index));
    if (ObjectList* list = wxGetApp().obj_list()) {
        list->update_info_items(resolved->index);
        list->add_volumes_to_object_in_list(resolved->index);
    }
    m_plater.notify_project_state_changed(ProjectStateChangeReason::Objects);
    result.changed = true;
    measure(result.open_edges_after, result.facets_after, result.parts_after, result.volume_after);
    return CommandResult::success();
}

} // namespace Slic3r::GUI::JusPrin::Workspace
