#pragma once

// Geometry helpers shared by the adapter's analysis, placement and region
// code: feature handles and frames. Orca-facing; not GUI-free.

#include "Workspace.hpp"
#include "Regions.hpp"
#include "libslic3r/Measure.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Workspace {

inline Vec3 vec3(const Vec3d& v) { return {v.x(), v.y(), v.z()}; }

// "f<revision>-<object>-<volume>-<plane>-<feature>": everything needed to find
// the feature again in the same revision.
struct FeatureAddress
{
    std::uint64_t revision{0}, object{0};
    std::size_t   volume{0};
    int           plane{0}, feature{0};
};

inline std::string feature_handle(const FeatureAddress& address)
{
    return "f" + std::to_string(address.revision) + "-" + std::to_string(address.object) + "-" +
           std::to_string(address.volume) + "-" + std::to_string(address.plane) + "-" + std::to_string(address.feature);
}

inline std::optional<FeatureAddress> parse_feature_handle(const std::string& text)
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
inline bool covered_by(const indexed_triangle_set& its, const std::vector<int>& triangles, const Vec3d& point)
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

inline Transform3d world_of(const ModelObject& object, const ModelVolume& volume)
{
    return object.instances.front()->get_matrix() * volume.get_matrix();
}

// Region and hole geometry, in a model part's mesh frame.
inline constexpr double kPositionTolerance = 0.05; // mm
inline constexpr double kAngleTolerance    = 0.5 * PI / 180.0;

inline Vec3d eigen(const Vec3& v) { return {v[0], v[1], v[2]}; }

inline std::vector<ModelVolume*> model_parts(const ModelObject& object)
{
    std::vector<ModelVolume*> parts;
    for (ModelVolume* volume : object.volumes)
        if (volume->is_model_part())
            parts.push_back(volume);
    return parts;
}

// The outward normal of a Measure plane, in the part's mesh frame.
inline std::optional<Vec3d> plane_normal(const Measure::Measuring& measuring, int plane)
{
    for (const Measure::SurfaceFeature& feature : measuring.get_plane_features(plane))
        if (feature.get_type() == Measure::SurfaceFeatureType::Plane) {
            const auto [unused, normal, point] = feature.get_plane();
            if (normal.allFinite() && normal.norm() > 0)
                return normal.normalized();
        }
    return std::nullopt;
}

inline double facets_area(const indexed_triangle_set& its, const std::vector<int>& facets)
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
inline std::vector<int> match_face(const indexed_triangle_set& its, const RegionGeometry& face)
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
inline RegionGeometry hole_from(const indexed_triangle_set& its, const Measure::Measuring& measuring, int plane, int feature)
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

inline bool same_hole(const indexed_triangle_set& its, const RegionGeometry& stored)
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

inline std::vector<int> facets_facing(const indexed_triangle_set& its, const Vec3d& direction, double tolerance_degrees)
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
inline std::vector<int> facets_of(const indexed_triangle_set& its, const RegionGeometry& geometry)
{
    if (geometry.type == "face")
        return match_face(its, geometry);
    return facets_facing(its, eigen(geometry.normal).normalized(), geometry.tolerance_degrees);
}

// The primitive's mesh in the object's frame: `part` places the part's mesh
// frame, in which the geometry is stored.
inline TriangleMesh primitive_mesh(const RegionGeometry& geometry, const Transform3d& part)
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

// The space a hole takes: a cylinder from its mouth into the part, centred
// halfway.
inline RegionGeometry hole_cylinder(RegionGeometry geometry)
{
    geometry.type   = "cylinder";
    geometry.center = vec3(eigen(geometry.center) - 0.5 * geometry.length * eigen(geometry.normal).normalized());
    return geometry;
}

// The volume a hole region generates. A support blocker must cover the
// overhang where Orca detects it, and near the crown of a round hole that is
// wider than the hole's own slice, so it is a millimetre larger all round and
// past each mouth; the blocker only ever removes support. A reinforcing ring
// is the hole plus the wall around it.
inline RegionGeometry volume_geometry(const RegionRecord& record)
{
    if (record.geometry.type != "hole")
        return record.geometry;
    RegionGeometry geometry = hole_cylinder(record.geometry);
    geometry.diameter += record.kind == "reinforce" ? 4.0 : 2.0;
    if (record.kind != "reinforce")
        geometry.length += 2.0;
    return geometry;
}


} // namespace Slic3r::GUI::JusPrin::Workspace
