#pragma once

// Immutable manufacturing facts stored with a JusPrin project. Builds and
// exported copies belong to the editable project timeline; physical prints
// form a separate factual ledger and therefore survive Revert.

#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

struct SliceStatistics
{
    double        print_time_seconds{0.0};
    double        filament_mm{0.0};
    double        material_grams{0.0};
    double        material_cost{0.0};
    std::uint64_t layer_count{0};
};

struct BuildRecord
{
    std::string   id;
    std::uint64_t seq{0};
    std::string   created_at;
    std::string   project_id;
    std::string   revision_id;
    std::string   conversation_id;
    std::string   after_message_id;
    std::size_t   plate_index{0};
    std::string   plate_name;
    std::string   printer;
    std::string   material;
    std::string   manufacturing_input_hash;
    std::string   output_hash;
    std::string   slicer_version;
    std::string   configuration_provenance;
    SliceStatistics statistics;
    std::vector<std::string> warnings;
};

struct ExportedCopyRecord
{
    std::string   id;
    std::uint64_t seq{0};
    std::string   created_at;
    std::string   build_id;
    std::string   conversation_id;
    std::string   after_message_id;
    std::string   destination;
    std::string   expected_output_hash;
    // Optional checksum observed at the destination. Whether the copy matches
    // is always derived from the two hashes; no mutable "modified" flag is
    // persisted.
    std::string observed_output_hash;
};

struct PhysicalPrintRecord
{
    std::string   id;
    std::uint64_t seq{0};
    std::string   started_at;
    std::string   ended_at;
    std::string   outcome; // completed|failed|cancelled
    std::string   failure;
    std::string   build_id;
    std::string   project_id;
    std::string   revision_id;
    std::string   conversation_id;
    std::string   after_message_id;
    std::size_t   plate_index{0};
    std::string   plate_name;
    std::string   printer;
    std::string   material;
    std::string   manufacturing_input_hash;
    std::string   output_hash;
    std::string   gcode_hash;
    SliceStatistics statistics;
};

// Stable lowercase SHA-256 for persisted provenance.
std::string sha256_hex(const std::string& bytes);

// Canonical hash of the manufacturing facts exposed by IWorkspace for one
// plate. It deliberately excludes selection, undo state, dirty state, object
// names, and the process-local project session. A missing plate has no hash.
std::optional<std::string> manufacturing_input_hash(const Workspace::WorkspaceSnapshot& snapshot,
                                                    std::size_t                         plate_index);

// Deterministic G-code identity used by the typed Phase Six record workflow.
// A production slicer adapter may replace the source bytes, but the persisted
// contract remains an immutable SHA-256 string.
std::string deterministic_output_hash(const std::string& manufacturing_hash,
                                      const std::string& slicer_version,
                                      const std::string& configuration_provenance);

} // namespace Slic3r::GUI::JusPrin::Agent
