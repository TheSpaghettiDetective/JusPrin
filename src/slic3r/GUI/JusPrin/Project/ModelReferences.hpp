#pragma once

// What the main model file of a split project archive (3D/3dmodel.model)
// refers to: each <component p:path="/3D/Objects/…" objectid="…"/> names a
// mesh entry and one <object id> inside it. Grouped by entry, in order of
// first appearance, with each entry's ids in order of first appearance --
// which is the order the ids appear inside the entry, because the exporter
// writes an object's parts and its components in the same order and a part
// shared with another object refers back to the owner's entry.
//
// GUI-free, no OrcaSlicer types.

#include <cstdint>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Project {

struct MeshReference
{
    std::string                path; // archive entry name, no leading slash, XML entities resolved
    std::vector<std::uint64_t> ids;
};

std::vector<MeshReference> mesh_references(const std::string& main_model_text);

} // namespace Slic3r::GUI::JusPrin::Project
