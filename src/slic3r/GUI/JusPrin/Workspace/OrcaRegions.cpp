// Region annotations against the live Orca model: resolving a request into a
// record, generating its artifacts (support and seam paint, support blockers
// and enforcers, modifier volumes, object overrides), removing them, and
// checking whether a stored record still matches the model.

#include "OrcaWorkspaceAdapter.hpp"
#include "OrcaGeometry.hpp"
#include "OrcaSettings.hpp"
#include "Regions.hpp"

#include "libslic3r/Measure.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosManager.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <wx/thread.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace Slic3r::GUI::JusPrin::Workspace {
namespace {

constexpr double kPositionTolerance = 0.05; // mm
constexpr double kAngleTolerance    = 0.5 * PI / 180.0;

// A painted facet's state, read without the GUI's selector. The original
// facets are the first entries of the selector's triangle list.
class PaintReader : public TriangleSelector
{
public:
    using TriangleSelector::TriangleSelector;
    bool painted(int facet, EnforcerBlockerType state) const
    {
        if (facet < 0 || facet >= static_cast<int>(m_triangles.size()))
            return false;
        const Triangle& triangle = m_triangles[facet];
        return triangle.valid() && !triangle.is_split() && triangle.get_state() == state;
    }
};

Vec3d eigen(const Vec3& v) { return {v[0], v[1], v[2]}; }

std::vector<ModelVolume*> model_parts(const ModelObject& object)
{
    std::vector<ModelVolume*> parts;
    for (ModelVolume* volume : object.volumes)
        if (volume->is_model_part())
            parts.push_back(volume);
    return parts;
}

std::vector<std::size_t> part_facets(const ModelObject& object)
{
    std::vector<std::size_t> counts;
    for (const ModelVolume* part : model_parts(object))
        counts.push_back(part->mesh().facets_count());
    return counts;
}

FacetsAnnotation& paint_layer(ModelVolume& part, const std::string& target)
{
    return target == "support" ? part.supported_facets : target == "seam" ? part.seam_facets : part.mmu_segmentation_facets;
}

EnforcerBlockerType paint_state(const std::string& state)
{
    if (state == "enforcer")
        return EnforcerBlockerType::ENFORCER;
    if (state == "blocker")
        return EnforcerBlockerType::BLOCKER;
    return static_cast<EnforcerBlockerType>(std::stoi(state));
}

ModelVolumeType volume_type(const std::string& target)
{
    return target == "support_blocker"  ? ModelVolumeType::SUPPORT_BLOCKER :
           target == "support_enforcer" ? ModelVolumeType::SUPPORT_ENFORCER : ModelVolumeType::PARAMETER_MODIFIER;
}

int find_volume(const ModelObject& object, const RegionArtifact& artifact)
{
    for (std::size_t index = 0; index < object.volumes.size(); ++index)
        if (object.volumes[index]->name == artifact.name && object.volumes[index]->type() == volume_type(artifact.target))
            return static_cast<int>(index);
    return -1;
}

// The outward normal of a Measure plane, in the part's mesh frame.
std::optional<Vec3d> plane_normal(const Measure::Measuring& measuring, int plane)
{
    for (const Measure::SurfaceFeature& feature : measuring.get_plane_features(plane))
        if (feature.get_type() == Measure::SurfaceFeatureType::Plane) {
            const auto [unused, normal, point] = feature.get_plane();
            if (normal.allFinite() && normal.norm() > 0)
                return normal.normalized();
        }
    return std::nullopt;
}

double facets_area(const indexed_triangle_set& its, const std::vector<int>& facets)
{
    double area = 0;
    for (int index : facets) {
        const auto& face = its.indices[index];
        const Vec3d a = its.vertices[face[0]].cast<double>(), b = its.vertices[face[1]].cast<double>(),
                    c = its.vertices[face[2]].cast<double>();
        area += 0.5 * (b - a).cross(c - a).norm();
    }
    return area;
}

// A stored face, found again: the plane with the same normal, lying in the
// same plane, with the same area.
std::vector<int> match_face(const indexed_triangle_set& its, const RegionGeometry& face)
{
    const Measure::Measuring measuring(its);
    const Vec3d              normal = eigen(face.normal).normalized();
    for (int plane = 0; plane < measuring.get_num_of_planes(); ++plane) {
        const auto found = plane_normal(measuring, plane);
        if (!found || std::acos(std::clamp(found->dot(normal), -1.0, 1.0)) > kAngleTolerance)
            continue;
        const std::vector<int>& facets = measuring.get_plane_triangle_indices(plane);
        const auto&             first  = its.indices[facets.front()];
        if (std::abs(normal.dot(eigen(face.center) - its.vertices[first[0]].cast<double>())) > kPositionTolerance)
            continue;
        const double area = facets_area(its, facets);
        if (std::abs(area - face.area) <= 0.02 * face.area + 1e-6)
            return facets;
    }
    return {};
}

// A hole's mouth and its depth, in the part's mesh frame: the matching mouth
// on the far side when the hole goes through, otherwise the part's extent.
RegionGeometry hole_from(const indexed_triangle_set& its, const Measure::Measuring& measuring, int plane, int feature)
{
    const auto [center, radius, circle_normal] = measuring.get_plane_features(plane)[feature].get_circle();
    const Vec3d    normal = plane_normal(measuring, plane).value_or(circle_normal.normalized());
    RegionGeometry hole;
    hole.type     = "hole";
    hole.center   = vec3(center);
    hole.normal   = vec3(normal);
    hole.diameter = 2 * radius;
    double depth = 0;
    for (int other = 0; other < measuring.get_num_of_planes(); ++other)
        for (const Measure::SurfaceFeature& candidate : measuring.get_plane_features(other)) {
            if (candidate.get_type() != Measure::SurfaceFeatureType::Circle || (other == plane))
                continue;
            const auto [far_center, far_radius, unused] = candidate.get_circle();
            const Vec3d offset = far_center - center;
            const double along = offset.dot(normal);
            if (std::abs(far_radius - radius) <= kPositionTolerance && along < 0 &&
                (offset - along * normal).norm() <= kPositionTolerance)
                depth = std::max(depth, -along);
        }
    if (depth <= 0)
        for (const stl_vertex& vertex : its.vertices)
            depth = std::max(depth, (center - vertex.cast<double>()).dot(normal));
    hole.length = depth;
    return hole;
}

bool same_hole(const indexed_triangle_set& its, const RegionGeometry& stored)
{
    const Measure::Measuring measuring(its);
    for (int plane = 0; plane < measuring.get_num_of_planes(); ++plane)
        for (const Measure::SurfaceFeature& feature : measuring.get_plane_features(plane))
            if (feature.get_type() == Measure::SurfaceFeatureType::Circle) {
                const auto [center, radius, unused] = feature.get_circle();
                if ((center - eigen(stored.center)).norm() <= kPositionTolerance &&
                    std::abs(2 * radius - stored.diameter) <= kPositionTolerance)
                    return true;
            }
    return false;
}

std::vector<int> facets_facing(const indexed_triangle_set& its, const Vec3d& direction, double tolerance_degrees)
{
    std::vector<int> facets;
    const double     limit = std::cos(tolerance_degrees * PI / 180.0);
    for (int index = 0; index < static_cast<int>(its.indices.size()); ++index) {
        const auto& face = its.indices[index];
        Vec3d normal = (its.vertices[face[1]] - its.vertices[face[0]]).cast<double>().cross((its.vertices[face[2]] - its.vertices[face[0]]).cast<double>());
        if (normal.norm() > 0 && normal.normalized().dot(direction) >= limit)
            facets.push_back(index);
    }
    return facets;
}

// The facets a stored face or direction paints, on the part as it is now.
std::vector<int> facets_of(const indexed_triangle_set& its, const RegionGeometry& geometry)
{
    if (geometry.type == "face")
        return match_face(its, geometry);
    return facets_facing(its, eigen(geometry.normal).normalized(), geometry.tolerance_degrees);
}

// The primitive's mesh in the object's frame: `part` places the part's mesh
// frame, in which the geometry is stored.
TriangleMesh primitive_mesh(const RegionGeometry& geometry, const Transform3d& part)
{
    // Stored boxes are aligned with the part's own axes, so only cylinders
    // need a basis.
    Transform3d local = Transform3d::Identity();
    indexed_triangle_set its;
    if (geometry.type == "box") {
        its = its_make_cube(geometry.size[0], geometry.size[1], geometry.size[2]);
        local.translate(eigen(geometry.center) - 0.5 * eigen(geometry.size));
    } else {
        its = its_make_cylinder(0.5 * geometry.diameter, geometry.length, 2 * PI / 48);
        const Vec3d z = eigen(geometry.normal).normalized();
        Vec3d       x = std::abs(z.x()) < 0.9 ? Vec3d::UnitX() : Vec3d::UnitY();
        x = (x - z * z.dot(x)).normalized();
        Matrix3d basis;
        basis.col(0) = x;
        basis.col(1) = z.cross(x);
        basis.col(2) = z;
        local.translate(eigen(geometry.center) - 0.5 * geometry.length * z);
        local.rotate(basis);
    }
    TriangleMesh mesh(std::move(its));
    mesh.transform(part * local);
    return mesh;
}

// A hole's volume runs from its mouth into the part, centred halfway; a
// reinforcing ring is the hole plus the wall around it.
RegionGeometry volume_geometry(const RegionRecord& record)
{
    RegionGeometry geometry = record.geometry;
    if (geometry.type == "hole") {
        geometry.type   = "cylinder";
        geometry.center = vec3(eigen(geometry.center) - 0.5 * geometry.length * eigen(geometry.normal).normalized());
        if (record.kind == "reinforce")
            geometry.diameter += 4.0;
    }
    return geometry;
}

} // namespace

std::optional<std::size_t> OrcaWorkspaceAdapter::region_object(const RegionRecord& record) const
{
    const Model& model = m_plater.model();
    std::optional<std::size_t> by_name;
    for (std::size_t index = 0; index < model.objects.size(); ++index) {
        const ModelObject& object = *model.objects[index];
        if (part_facets(object) != record.part_facets)
            continue;
        if (record.session == m_session.value() && object.id().id == record.object)
            return index;
        if (!by_name && object.name == record.object_name)
            by_name = index;
    }
    return by_name;
}

CommandResult OrcaWorkspaceAdapter::plan_regions(const std::vector<RegionRequest>& requests, const std::vector<RegionRecord>& stored,
                                                 std::vector<RegionRecord>& planned) const
{
    wxASSERT(wxIsMainThread());
    planned.clear();
    if (requests.empty() || requests.size() > kRegionLimit)
        return CommandResult::failure(WorkspaceError::InvalidArgument, "Annotate 1 to 32 regions at a time");
    const Model& model = m_plater.model();
    std::set<std::string> seen;
    for (const RegionRequest& request : requests) {
        const RegionRecord* existing = nullptr;
        if (!request.region_id.empty()) {
            for (const RegionRecord& record : stored)
                if (record.id == request.region_id)
                    existing = &record;
            if (existing == nullptr)
                return CommandResult::failure(WorkspaceError::InvalidArgument,
                                              "There is no region " + request.region_id + "; read object_analyze's regions section");
            if (!seen.insert(request.region_id).second)
                return CommandResult::failure(WorkspaceError::InvalidArgument, "Region " + request.region_id + " is named twice");
        }
        RegionRecord record;
        record.kind = request.kind.empty() && existing ? existing->kind : request.kind;
        if (!region_kind(record.kind))
            return CommandResult::failure(WorkspaceError::InvalidArgument, "\"" + record.kind + "\" is not a region kind");
        const std::string geometry_type = request.geometry ? request.geometry->type : existing ? existing->geometry.type : std::string();
        if (!geometry_type.empty() && !region_kind_accepts(record.kind, geometry_type))
            return CommandResult::failure(WorkspaceError::InvalidArgument, "A " + record.kind + " region cannot be a " + geometry_type);

        std::optional<std::size_t> index;
        if (request.object) {
            const auto resolved = resolve(*request.object);
            if (!resolved)
                return id_error(*request.object);
            index = resolved->index;
        } else if (existing) {
            index = region_object(*existing);
            if (!index)
                return CommandResult::failure(WorkspaceError::StaleId,
                                              "Region " + existing->id + "'s object is no longer in the project; annotate it again with an objectId");
        } else {
            return CommandResult::failure(WorkspaceError::InvalidArgument, "A new region needs an objectId");
        }
        const ModelObject& object = *model.objects[*index];
        const auto         parts  = model_parts(object);
        if (parts.empty() || object.instances.empty())
            return CommandResult::failure(WorkspaceError::UnavailableOperation, object.name + " has no solid part to annotate");
        if (!request.object && existing && request.geometry == std::nullopt && existing->part >= parts.size())
            return CommandResult::failure(WorkspaceError::StaleId, "Region " + existing->id + "'s part is gone");

        record.id          = existing ? existing->id : next_region_id(stored, planned);
        record.session     = m_session.value();
        record.object      = object.id().id;
        record.object_name = object.name;
        record.part_facets = part_facets(object);

        if (request.geometry) {
            const RegionGeometry& asked = *request.geometry;
            record.geometry.type        = asked.type;
            if (!asked.handle.empty()) {
                const auto address = parse_feature_handle(asked.handle);
                if (!address || address->object != object.id().id || address->volume >= object.volumes.size() ||
                    !object.volumes[address->volume]->is_model_part())
                    return CommandResult::failure(WorkspaceError::InvalidArgument, asked.handle + " is not a feature of " + object.name);
                if (address->revision != m_changes.revision())
                    return CommandResult::failure(WorkspaceError::FeatureExpired,
                                                  "The project changed since " + asked.handle + " was read; analyze the object again");
                const ModelVolume&          part = *object.volumes[address->volume];
                const indexed_triangle_set& its  = part.mesh().its;
                const Measure::Measuring    measuring(its);
                if (address->plane >= measuring.get_num_of_planes() ||
                    address->feature >= static_cast<int>(measuring.get_plane_features(address->plane).size()))
                    return CommandResult::failure(WorkspaceError::InvalidArgument, asked.handle + " is not a feature of " + object.name);
                const Measure::SurfaceFeature& feature = measuring.get_plane_features(address->plane)[address->feature];
                record.part = static_cast<std::size_t>(std::find(parts.begin(), parts.end(), &part) - parts.begin());
                if (asked.type == "face" && feature.get_type() == Measure::SurfaceFeatureType::Plane) {
                    const std::vector<int>& facets = measuring.get_plane_triangle_indices(address->plane);
                    const auto&             first  = its.indices[facets.front()];
                    record.geometry.normal = vec3(plane_normal(measuring, address->plane).value_or(Vec3d::UnitZ()));
                    record.geometry.center = vec3(its.vertices[first[0]].cast<double>());
                    record.geometry.area   = facets_area(its, facets);
                } else if (asked.type == "hole" && feature.get_type() == Measure::SurfaceFeatureType::Circle) {
                    record.geometry = hole_from(its, measuring, address->plane, address->feature);
                } else {
                    return CommandResult::failure(WorkspaceError::InvalidArgument, asked.handle + " is not a " + asked.type + " handle");
                }
            } else if (asked.type == "box" || asked.type == "cylinder" || asked.type == "direction") {
                // Given as the object stands now; kept in the first part's mesh
                // frame, so moving the object later does not move the region.
                record.part                  = 0;
                const Transform3d world      = world_of(object, *parts.front());
                const Transform3d to_mesh    = world.inverse();
                const Matrix3d    linear     = world.linear();
                if (asked.type == "direction") {
                    const Vec3d direction = eigen(asked.normal);
                    if (direction.norm() < 1e-9 || asked.tolerance_degrees <= 0 || asked.tolerance_degrees > 90)
                        return CommandResult::failure(WorkspaceError::InvalidArgument, "A direction needs a vector and a tolerance of 0 to 90 degrees");
                    record.geometry.normal            = vec3((linear.transpose() * direction).normalized());
                    record.geometry.tolerance_degrees = asked.tolerance_degrees;
                } else {
                    record.geometry.center = vec3(to_mesh * eigen(asked.center));
                    if (asked.type == "box") {
                        // World-aligned now; stored as its extent along the part's axes.
                        const Vec3d half = 0.5 * eigen(asked.size);
                        if (half.minCoeff() <= 0)
                            return CommandResult::failure(WorkspaceError::InvalidArgument, "A box needs a positive size");
                        const Matrix3d back = linear.inverse();
                        Vec3d extent = Vec3d::Zero();
                        for (int axis = 0; axis < 3; ++axis)
                            extent += (back * (half[axis] * Vec3d::Unit(axis))).cwiseAbs();
                        record.geometry.size = vec3(2 * extent);
                    } else {
                        const Vec3d axis = eigen(asked.normal);
                        if (axis.norm() < 1e-9 || asked.diameter <= 0 || asked.length <= 0)
                            return CommandResult::failure(WorkspaceError::InvalidArgument, "A cylinder needs an axis, a diameter and a length");
                        const Vec3d mesh_axis = linear.inverse() * axis.normalized();
                        const double scale    = mesh_axis.norm();
                        record.geometry.normal   = vec3(mesh_axis.normalized());
                        record.geometry.length   = asked.length * scale;
                        record.geometry.diameter = asked.diameter * scale;
                    }
                }
            } else if (asked.type == "object") {
                record.part = 0;
            } else {
                return CommandResult::failure(WorkspaceError::InvalidArgument,
                                              "A " + asked.type + " region needs a handle from object_analyze");
            }
        } else if (existing) {
            record.geometry = existing->geometry;
            record.part     = existing->part;
        } else {
            return CommandResult::failure(WorkspaceError::InvalidArgument, "A new region needs a geometry");
        }
        if (!region_kind_accepts(record.kind, record.geometry.type))
            return CommandResult::failure(WorkspaceError::InvalidArgument,
                                          "A " + record.kind + " region cannot be a " + record.geometry.type);

        record.extruder = request.extruder != 0 ? request.extruder : existing ? existing->extruder : 0;
        if (record.kind == "material") {
            const int filaments = static_cast<int>(wxGetApp().preset_bundle->filament_presets.size());
            if (record.extruder < 1 || record.extruder > filaments)
                return CommandResult::failure(WorkspaceError::InvalidArgument,
                                              "A material region needs an extruder from 1 to " + std::to_string(filaments));
        }
        record.settings = !request.settings.empty() ? request.settings :
                          existing && existing->kind == record.kind ? existing->settings : region_default_settings(record.kind);
        if (record.kind == "reinforce" || record.kind == "flexible") {
            DynamicPrintConfig check = wxGetApp().preset_bundle->prints.get_edited_preset().config;
            for (const auto& [key, value] : record.settings) {
                // A modifier volume carries region settings; an object override
                // may carry object settings too.
                const bool region = PrintRegionConfig().optptr(key) != nullptr;
                if (!writable_setting(key) || !(region || (record.geometry.type == "object" && object_setting(key))))
                    return CommandResult::failure(WorkspaceError::InvalidArgument, key + " cannot be set on a region");
                if (auto refused = set_setting_value(check, key, value))
                    return CommandResult::failure(WorkspaceError::InvalidArgument, key + ": " + *refused);
                record.settings[key] = check.option(key)->serialize();
            }
        } else if (!request.settings.empty()) {
            return CommandResult::failure(WorkspaceError::InvalidArgument, "Only reinforce and flexible regions take settings");
        }

        record.artifacts = region_artifact_plan(record);
        for (RegionArtifact& artifact : record.artifacts) {
            if (artifact.type != "paint")
                continue;
            artifact.facets = facets_of(parts[record.part]->mesh().its, record.geometry);
            if (artifact.facets.empty())
                return CommandResult::failure(WorkspaceError::StaleId, "No facet of " + object.name + " matches that face any more");
        }
        record.label = region_label(record);
        planned.push_back(std::move(record));
    }
    return CommandResult::success();
}

void OrcaWorkspaceAdapter::remove_region_artifacts(ModelObject& object, const RegionRecord& record)
{
    const auto parts = model_parts(object);
    for (const RegionArtifact& artifact : record.artifacts) {
        if (artifact.type == "volume") {
            const int index = find_volume(object, artifact);
            // The last solid part stays; region volumes are never solid.
            if (index >= 0)
                object.delete_volume(static_cast<std::size_t>(index));
        } else if (artifact.type == "paint") {
            if (artifact.part >= parts.size())
                continue;
            ModelVolume&      part  = *parts[artifact.part];
            FacetsAnnotation& layer = paint_layer(part, artifact.target);
            PaintReader       paint(part.mesh());
            paint.deserialize(layer.get_data(), false);
            const EnforcerBlockerType state = paint_state(artifact.state);
            for (int facet : artifact.facets)
                if (facet < static_cast<int>(part.mesh().its.indices.size()) && paint.painted(facet, state))
                    paint.set_facet(facet, EnforcerBlockerType::NONE);
            layer.set(paint);
        } else if (object.config.has(artifact.target) && object.config.option(artifact.target)->serialize() == artifact.state) {
            object.config.erase(artifact.target);
        }
    }
}

void OrcaWorkspaceAdapter::generate_region_artifacts(ModelObject& object, RegionRecord& record)
{
    const auto parts = model_parts(object);
    for (RegionArtifact& artifact : record.artifacts) {
        if (artifact.type == "volume") {
            const RegionGeometry geometry = volume_geometry(record);
            ModelVolume* volume = object.add_volume(primitive_mesh(geometry, parts[record.part]->get_matrix()), volume_type(artifact.target));
            volume->name = artifact.name;
            volume->config.set_key_value("extruder", new ConfigOptionInt(artifact.target == "modifier" && record.kind == "material" ? record.extruder : 0));
            for (const auto& [key, value] : record.settings) {
                DynamicPrintConfig parsed;
                parsed.set_deserialize_strict(key, value);
                volume->config.set_key_value(key, parsed.option(key)->clone());
            }
        } else if (artifact.type == "paint") {
            ModelVolume&      part  = *parts[artifact.part];
            FacetsAnnotation& layer = paint_layer(part, artifact.target);
            TriangleSelector  paint(part.mesh());
            paint.deserialize(layer.get_data(), false);
            const EnforcerBlockerType state = paint_state(artifact.state);
            for (int facet : artifact.facets)
                paint.set_facet(facet, state);
            layer.set(paint);
        } else {
            DynamicPrintConfig parsed;
            parsed.set_deserialize_strict(artifact.target, artifact.state);
            object.config.set_key_value(artifact.target, parsed.option(artifact.target)->clone());
        }
    }
}

CommandResult OrcaWorkspaceAdapter::change_regions(const std::vector<RegionRecord>& removed, const std::vector<RegionRecord>& added,
                                                   const char* snapshot_name)
{
    Model& model = m_plater.model();
    // An open paint gizmo keeps its own copy of the paint and would write it
    // back over ours.
    m_plater.canvas3D()->get_gizmos_manager().reset_all_states();
    const ProjectStateTransaction transaction = m_plater.project_state_transaction();
    Plater::TakeSnapshot          snapshot(&m_plater, snapshot_name);
    std::set<std::size_t>         touched;
    for (const RegionRecord& record : removed)
        if (const auto index = region_object(record)) {
            remove_region_artifacts(*model.objects[*index], record);
            touched.insert(*index);
        }
    for (const RegionRecord& record : added) {
        const auto resolved = resolve(ObjectId(m_session, record.object));
        if (!resolved)
            return CommandResult::failure(WorkspaceError::StaleId, record.object_name + " is no longer in the project");
        RegionRecord generated = record;
        generate_region_artifacts(*model.objects[resolved->index], generated);
        touched.insert(resolved->index);
    }
    ObjectList* list = wxGetApp().obj_list();
    for (std::size_t index : touched) {
        if (list) {
            list->add_volumes_to_object_in_list(index);
            list->object_config_options_changed({model.objects[index], nullptr});
            list->update_info_items(index);
        }
        m_plater.changed_object(static_cast<int>(index));
    }
    m_plater.notify_project_state_changed(ProjectStateChangeReason::Objects);
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::apply_regions(const std::vector<RegionRecord>& planned, const std::vector<RegionRecord>& replaced,
                                                  std::vector<RegionRecord>& applied)
{
    wxASSERT(wxIsMainThread());
    const CommandResult result = change_regions(replaced, planned, "Annotate regions");
    if (result.succeeded())
        applied = planned;
    return result;
}

CommandResult OrcaWorkspaceAdapter::remove_regions(const std::vector<RegionRecord>& records)
{
    wxASSERT(wxIsMainThread());
    return change_regions(records, {}, "Remove regions");
}

std::vector<RegionStatus> OrcaWorkspaceAdapter::region_status(const std::vector<RegionRecord>& records) const
{
    wxASSERT(wxIsMainThread());
    std::vector<RegionStatus> statuses;
    const Model& model = m_plater.model();
    for (const RegionRecord& record : records) {
        RegionStatus status{record, std::nullopt, true, true};
        const auto   index = region_object(record);
        if (index) {
            const ModelObject& object = *model.objects[*index];
            const auto         parts  = model_parts(object);
            // A matching object has the recorded parts, so at least one.
            status.object             = ObjectId(m_session, object.id().id);
            const indexed_triangle_set& its = parts[std::min(record.part, parts.size() - 1)]->mesh().its;
            status.binding_lost = record.geometry.type == "face" ? match_face(its, record.geometry).empty() :
                                  record.geometry.type == "hole" ? !same_hole(its, record.geometry) : false;
            status.artifacts_missing = std::any_of(record.artifacts.begin(), record.artifacts.end(), [&](const RegionArtifact& artifact) {
                if (artifact.type == "volume")
                    return find_volume(object, artifact) < 0;
                if (artifact.type == "paint") {
                    if (artifact.part >= parts.size())
                        return true;
                    ModelVolume& part = *parts[artifact.part];
                    PaintReader  paint(part.mesh());
                    paint.deserialize(paint_layer(part, artifact.target).get_data(), false);
                    const EnforcerBlockerType state = paint_state(artifact.state);
                    return !std::all_of(artifact.facets.begin(), artifact.facets.end(),
                                        [&](int facet) { return paint.painted(facet, state); });
                }
                return !object.config.has(artifact.target) || object.config.option(artifact.target)->serialize() != artifact.state;
            });
        }
        statuses.push_back(std::move(status));
    }
    return statuses;
}

} // namespace Slic3r::GUI::JusPrin::Workspace
