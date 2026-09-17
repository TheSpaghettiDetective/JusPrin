// Checks on a finished slice that Orca does not report itself: support that
// enters a hole or a region kept free of support, seams on faces a region
// names, first-layer contact, and slices that start in the air. Read on the
// GUI thread from a plate whose slice is current, as the preview does.

#include "OrcaWorkspaceAdapter.hpp"
#include "OrcaGeometry.hpp"
#include "Regions.hpp"

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "slic3r/GUI/PartPlate.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r::GUI::JusPrin::Workspace {
namespace {

constexpr std::size_t kCheckLimit = 32;

double area_mm2(const ExPolygons& polygons)
{
    double area = 0;
    for (const ExPolygon& polygon : polygons)
        area += polygon.area();
    return area * SCALING_FACTOR * SCALING_FACTOR;
}

// What support a layer printed, whichever generator made it.
ExPolygons support_of(const SupportLayer& layer)
{
    ExPolygons polygons = layer.support_type == stInnerTree ? layer.lslices : layer.support_islands;
    if (polygons.empty())
        polygons = union_ex(layer.support_fills.polygons_covered_by_spacing(float(SCALED_EPSILON)));
    return polygons;
}

// The closest point of a triangle to a point (Ericson, Real-Time Collision
// Detection, 5.1.5), for the distance from a seam to a face.
Vec3d closest_on_triangle(const Vec3d& p, const Vec3d& a, const Vec3d& b, const Vec3d& c)
{
    const Vec3d ab = b - a, ac = c - a, ap = p - a;
    const double d1 = ab.dot(ap), d2 = ac.dot(ap);
    if (d1 <= 0 && d2 <= 0) return a;
    const Vec3d bp = p - b;
    const double d3 = ab.dot(bp), d4 = ac.dot(bp);
    if (d3 >= 0 && d4 <= d3) return b;
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + (d1 / (d1 - d3)) * ab;
    const Vec3d cp = p - c;
    const double d5 = ab.dot(cp), d6 = ac.dot(cp);
    if (d6 >= 0 && d5 <= d6) return c;
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + (d2 / (d2 - d6)) * ac;
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + ((d4 - d3) / ((d4 - d3) + (d5 - d6))) * (c - b);
    const double denominator = 1 / (va + vb + vc);
    return a + ab * (vb * denominator) + ac * (vc * denominator);
}

const ModelObject* model_object_by_id(const Model& model, std::uint64_t id)
{
    for (const ModelObject* object : model.objects)
        if (object->id().id == id)
            return object;
    return nullptr;
}

} // namespace

void OrcaWorkspaceAdapter::check_slice(PartPlate& plate, const SliceReportRequest& request, SliceReport& report) const
{
    const Print* print = plate.fff_print();
    if (print == nullptr)
        return;
    const Model&                    model    = m_plater.model();
    const std::vector<RegionStatus> statuses = region_status(request.regions);
    const auto id_of = [this](const ModelObject* object) {
        return object ? std::optional<ObjectId>(ObjectId(m_session, object->id().id)) : std::nullopt;
    };

    if (request.supports) {
        SliceSupports supports;
        for (const PrintObject* object : print->objects()) {
            const ModelObject* source = object->model_object();
            if (source == nullptr || !object->is_step_done(posSupportMaterial) || object->support_layers().empty())
                continue;
            supports.generated = true;
            supports.layers    = std::max(supports.layers, object->support_layers().size());
            // Each support layer's footprint at its middle, in the object's
            // slicing frame.
            std::vector<float>      zs;
            std::vector<ExPolygons> printed;
            for (const SupportLayer* layer : object->support_layers()) {
                ExPolygons polygons = support_of(*layer);
                if (polygons.empty())
                    continue;
                zs.push_back(float(layer->print_z - 0.5 * layer->height - object->slicing_parameters().object_print_z_min));
                printed.push_back(std::move(polygons));
            }
            if (zs.empty())
                continue;

            struct Target
            {
                std::string    region_id, name;
                TriangleMesh   mesh;
                RegionGeometry hole; // in the object frame, for telling holes apart
                Vec3d          center;
            };
            std::vector<Target> targets;
            const auto          parts = model_parts(*source);
            const auto hole_in_object = [](const RegionGeometry& hole, const Transform3d& part) {
                RegionGeometry moved = hole;
                moved.center = vec3(part * eigen(hole.center));
                moved.normal = vec3((part.linear() * eigen(hole.normal)).normalized());
                return moved;
            };
            for (const RegionStatus& status : statuses) {
                const RegionRecord& region = status.record;
                if (!status.object || status.object->value() != source->id().id || status.binding_lost ||
                    (region.kind != "no_support" && region.kind != "precision_hole") || region.part >= parts.size() ||
                    (region.geometry.type != "hole" && region.geometry.type != "box" && region.geometry.type != "cylinder"))
                    continue;
                // The hole itself, not the larger blocker made for it.
                const Transform3d    part     = parts[region.part]->get_matrix();
                const RegionGeometry geometry = region.geometry.type == "hole" ? hole_cylinder(region.geometry) : region.geometry;
                const RegionKindInfo* info    = region_kind(region.kind);
                targets.push_back({region.id, "region " + region.id + " (" + (info ? info->words : region.kind) + ")",
                                   primitive_mesh(geometry, part),
                                   region.geometry.type == "hole" ? hole_in_object(region.geometry, part) : RegionGeometry{},
                                   part * eigen(geometry.center)});
            }
            // Holes the mesh has that no region names, each through-hole once.
            for (const ModelVolume* part : parts) {
                const indexed_triangle_set& its = part->mesh().its;
                const Measure::Measuring    measuring(its);
                for (int plane = 0; plane < measuring.get_num_of_planes(); ++plane) {
                    const auto& features = measuring.get_plane_features(plane);
                    for (int index = 0; index < static_cast<int>(features.size()); ++index) {
                        if (features[index].get_type() != Measure::SurfaceFeatureType::Circle)
                            continue;
                        const auto [center, radius, unused] = features[index].get_circle();
                        if (covered_by(its, measuring.get_plane_triangle_indices(plane), center))
                            continue;
                        RegionRecord hole;
                        hole.kind     = "no_support";
                        hole.geometry = hole_from(its, measuring, plane, index);
                        const RegionGeometry in_object = hole_in_object(hole.geometry, part->get_matrix());
                        const bool known = std::any_of(targets.begin(), targets.end(), [&](const Target& target) {
                            if (target.hole.type != "hole" || std::abs(target.hole.diameter - in_object.diameter) > kPositionTolerance)
                                return false;
                            const Vec3d axis   = eigen(target.hole.normal);
                            const Vec3d offset = eigen(in_object.center) - eigen(target.hole.center);
                            return std::abs(std::abs(axis.dot(eigen(in_object.normal))) - 1) < 1e-3 &&
                                   (offset - axis * axis.dot(offset)).norm() <= kPositionTolerance;
                        });
                        if (known)
                            continue;
                        const RegionGeometry geometry = hole_cylinder(hole.geometry);
                        targets.push_back({"", "hole of " + region_number(in_object.diameter) + " mm",
                                           primitive_mesh(geometry, part->get_matrix()), in_object,
                                           part->get_matrix() * eigen(geometry.center)});
                    }
                }
            }

            MeshSlicingParamsEx params;
            params.trafo = object->trafo_centered();
            for (const Target& target : targets) {
                const std::vector<ExPolygons> slices = slice_mesh_ex(target.mesh.its, zs, params);
                double      area   = 0;
                std::size_t layers = 0;
                for (std::size_t layer = 0; layer < slices.size() && layer < printed.size(); ++layer) {
                    if (slices[layer].empty())
                        continue;
                    // Support that only touches the rim is not inside.
                    const double inside = area_mm2(intersection_ex(offset_ex(slices[layer], -scale_(0.2)), printed[layer]));
                    if (inside > 0.01) {
                        area += inside;
                        ++layers;
                    }
                }
                if (area < 0.5)
                    continue;
                if (supports.contacts.size() == kCheckLimit) {
                    supports.truncated = true;
                    break;
                }
                const Vec3d world = source->instances.front()->get_matrix() * target.center;
                supports.contacts.push_back({id_of(source), source->name, target.region_id, target.name, area, layers, vec3(world)});
            }
        }
        report.supports = std::move(supports);
    }

    if (request.seams) {
        SliceSeams               seams;
        std::vector<Vec3d>       points;
        if (const GCodeProcessorResult* result = plate.get_slice_result())
            for (const GCodeProcessorResult::MoveVertex& move : result->moves)
                if (move.type == EMoveType::Seam)
                    points.push_back(move.position.cast<double>());
        seams.count = points.size();
        for (const RegionStatus& status : statuses) {
            const RegionRecord& region = status.record;
            if (!status.object || status.binding_lost || (region.geometry.type != "face" && region.geometry.type != "direction") ||
                (region.kind != "visible" && region.kind != "hidden" && region.kind != "seam_preferred" &&
                 region.kind != "seam_forbidden" && region.kind != "smooth_face"))
                continue;
            const ModelObject* object = model_object_by_id(model, status.object->value());
            if (object == nullptr)
                continue;
            const auto parts = model_parts(*object);
            if (region.part >= parts.size())
                continue;
            const indexed_triangle_set& its    = parts[region.part]->mesh().its;
            const std::vector<int>      facets = facets_of(its, region.geometry);
            std::size_t                 count  = 0;
            for (const ModelInstance* instance : object->instances) {
                const Transform3d to_mesh = (instance->get_matrix() * parts[region.part]->get_matrix()).inverse();
                const double      scale   = std::cbrt(std::abs(to_mesh.linear().determinant()));
                for (const Vec3d& point : points) {
                    const Vec3d local = to_mesh * point;
                    const bool touches = std::any_of(facets.begin(), facets.end(), [&](int index) {
                        const auto& face = its.indices[index];
                        const Vec3d a = its.vertices[face[0]].cast<double>(), b = its.vertices[face[1]].cast<double>(),
                                    c = its.vertices[face[2]].cast<double>();
                        return (closest_on_triangle(local, a, b, c) - local).norm() <= 0.6 * scale;
                    });
                    count += touches ? 1 : 0;
                }
            }
            if (seams.regions.size() < kCheckLimit)
                seams.regions.push_back({region.id, region.kind, object->name, count});
        }
        report.seams = std::move(seams);
    }

    if (request.first_layer) {
        SliceFirstLayer first;
        first.height = print->config().initial_layer_print_height.value;
        for (const PrintObject* object : print->objects()) {
            const ModelObject* source = object->model_object();
            if (source == nullptr || !object->is_step_done(posSlice) || object->layers().empty())
                continue;
            const double area = area_mm2(object->layers().front()->lslices) * double(object->instances().size());
            const auto   row  = std::find_if(first.objects.begin(), first.objects.end(),
                                             [&](const FirstLayerObject& o) { return o.object == id_of(source); });
            if (row != first.objects.end())
                row->contact_area += area;
            else if (first.objects.size() < kCheckLimit)
                first.objects.push_back({id_of(source), source->name, area, object->has_brim()});
        }
        report.first_layer = std::move(first);
    }

    if (request.islands) {
        SliceIslands islands;
        for (const PrintObject* object : print->objects()) {
            const ModelObject* source = object->model_object();
            if (source == nullptr || !object->is_step_done(posSlice))
                continue;
            for (const Layer* layer : object->layers()) {
                if (layer->lower_layer == nullptr)
                    continue;
                for (const ExPolygon& slice : layer->lslices) {
                    if (overlaps(offset_ex(slice, scale_(0.05)), layer->lower_layer->lslices))
                        continue;
                    const double area = area_mm2({slice});
                    if (area < 0.1)
                        continue;
                    // Held if a support layer just below it reaches it.
                    bool supported = false;
                    for (const SupportLayer* support : object->support_layers())
                        if (support->print_z <= layer->bottom_z() + EPSILON && support->print_z >= layer->bottom_z() - 2 * layer->height &&
                            overlaps(support_of(*support), ExPolygons{slice}))
                            supported = true;
                    if (islands.items.size() == kCheckLimit) {
                        islands.truncated = true;
                        break;
                    }
                    const Point  centre = slice.contour.centroid() + object->instances().front().shift;
                    islands.items.push_back({id_of(source), source->name, layer->print_z, area, supported,
                                             {unscale<double>(centre.x()), unscale<double>(centre.y()), layer->print_z}});
                }
            }
        }
        report.islands = std::move(islands);
    }
}

} // namespace Slic3r::GUI::JusPrin::Workspace
