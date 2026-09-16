#pragma once

// Geometry helpers shared by the adapter's analysis, placement and region
// code: feature handles and frames. Orca-facing; not GUI-free.

#include "Workspace.hpp"
#include "libslic3r/Model.hpp"

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

} // namespace Slic3r::GUI::JusPrin::Workspace
