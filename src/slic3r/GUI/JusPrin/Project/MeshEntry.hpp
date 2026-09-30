#pragma once

// A mesh entry is one file of a split project archive, 3D/Objects/<object
// name>_<backup id>.model: an XML document holding one <object id="…"> per
// part of a single object, each with that part's mesh and paint. The main
// model file refers to those ids by path.
//
// Between two archives of an unchanged object the entry differs only in
// those ids: a full save numbers objects in order, OrcaSlicer's backup uses
// each object's stable backup id (measured in
// agent-docs/jusprin/autosave-checkpoint-storage-findings.md, section 3). So a
// store keeps each entry once under the hash of its text with the ids
// blanked, remembers the ids the stored copy carries, and writes the ids a
// checkpoint's main model refers to at restore.
//
// GUI-free, no OrcaSlicer types.

#include <cstdint>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Project {

// The <object id="…"> values, in order of appearance.
std::vector<std::uint64_t> mesh_entry_ids(const std::string& text);

// SHA-256, lowercase hex, of the text with every <object id="…"> value
// blanked. Equal for the entries of an unchanged object in any two archives.
std::string mesh_entry_key(const std::string& text);

// The text with its k-th <object id="…"> set to ids[k]. Throws
// std::invalid_argument when the entry holds a different number of objects:
// a store never rewrites an entry that does not match its reference.
std::string rewrite_mesh_entry_ids(const std::string& text, const std::vector<std::uint64_t>& ids);

} // namespace Slic3r::GUI::JusPrin::Project
