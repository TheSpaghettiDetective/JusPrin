#pragma once

// Manufacturing facts stored with a JusPrin project: each plate the person
// sent to a printer, and each print a printer finished. The app writes both;
// the agent never does.

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

// A plate the person sent to a printer, written when they confirm Send.
struct SentRecord
{
    std::string   id;
    std::uint64_t seq{0};
    std::string   sent_at;
    // Set later, when the transport reports the job reached the printer;
    // empty until then, and for a transport that never says.
    std::string   delivery_confirmed_at;
    std::string   project_id;
    // The chat that was active when the plate was sent.
    std::string   conversation_id;
    std::size_t   plate_index{0};
    std::string   plate_name;
    std::string   printer;
    std::string   material;
    std::string   manufacturing_input_hash;
    // The slice's estimates for the plate as sent.
    SliceStatistics statistics;
};

// A print a printer finished, or that the person marked done.
struct PhysicalPrintRecord
{
    std::string   id;
    std::uint64_t seq{0};
    std::string   started_at;
    std::string   ended_at;
    std::string   outcome; // completed|failed|cancelled
    std::string   failure;
    // The Sent event this print ran from.
    std::string   sent_id;
    std::string   project_id;
    std::string   conversation_id;
    std::string   after_message_id;
    std::size_t   plate_index{0};
    std::string   plate_name;
    std::string   printer;
    std::string   material;
    std::string   manufacturing_input_hash;
    std::string   output_hash;
    std::string   gcode_hash;
    std::optional<int> stopped_percent;
    SliceStatistics statistics;
};

// Stable lowercase SHA-256 for persisted provenance.
std::string sha256_hex(const std::string& bytes);

// Canonical hash of the manufacturing facts exposed by IWorkspace for one
// plate. It deliberately excludes selection, undo state, dirty state, object
// names, and the process-local project session. A missing plate has no hash.
std::optional<std::string> manufacturing_input_hash(const Workspace::WorkspaceSnapshot& snapshot,
                                                    std::size_t                         plate_index);

// What a Sent event says about one plate of `snapshot`: the plate, the printer
// and material in force, its input hash and its slice estimates. The project,
// the chat and the time are the document's to add. Nothing for a plate the
// snapshot does not have.
std::optional<SentRecord> sent_record(const Workspace::WorkspaceSnapshot& snapshot, std::size_t plate_index);

} // namespace Slic3r::GUI::JusPrin::Agent
