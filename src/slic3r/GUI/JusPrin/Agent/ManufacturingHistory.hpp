#pragma once

// Manufacturing facts stored with a JusPrin project: one record per G-code
// that left the project, with where it went (exported files, sends to
// printers) and what the printer reported about each print of it. Slicing by
// itself writes nothing. The app writes the records; the agent never does.
//
// No failure is recorded anywhere: nothing reliably tells a failed print from
// one a person stopped, so a print that ended early simply never finishes.

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

// Where a G-code went. An export is a file JusPrin can hash again later; a
// send is a transfer to a printer, which is never verified like a file.
struct GcodeDestination
{
    enum class Kind { Export, Send };

    std::string   id;
    // In the document's one sequence, which orders it among the chat's items.
    std::uint64_t seq{0};
    Kind          kind{Kind::Export};
    std::string   at;
    // The chat that was active when it left, and the item it followed there:
    // where the chat's marker for it goes.
    std::string conversation_id;
    std::string after_id;
    // Export: the file written, and the hash of its bytes when it was written.
    std::string path;
    std::string file_hash;
    // Send: the printer by its own name, how to find it again (a Bambu device
    // id, a print host's address), the transport, and the file name the
    // printer was given.
    std::string printer_name;
    std::string printer_id;
    std::string transport; // bambu_print|bambu_storage|print_host
    std::string file_name;
    // Whether the send also asked the printer to start printing it.
    bool        start_requested{false};
    // When the transport reported the file on the printer; empty while that
    // is not confirmed.
    std::string delivered_at;
    // A Bambu send: the SHA-256 of the .gcode.3mf the printer was sent, which
    // the project keeps (keep_gcode_package). Empty for every other kind.
    std::string package_hash;
};

// A print of the G-code, only as the printer reported it.
struct GcodePrint
{
    std::string id;
    // The send it ran from.
    std::string destination_id;
    std::string printer_name;
    std::string printer_id;
    // The printer's own id for the job, to tell its later reports apart.
    std::string job_id;
    std::string started_at;
    // Empty until the printer reports the print finished, and forever for a
    // print that stopped early.
    std::string finished_at;
};

struct GcodeRecord
{
    // SHA-256 of the plate's sliced G-code bytes: the record's identity. The
    // same slice exported and sent is one record, whatever wrapping or
    // post-processing each route added to the file that left.
    std::string     hash;
    std::uint64_t   seq{0};
    std::string     created_at;
    std::size_t     plate_index{0};
    std::string     plate_name;
    std::string     printer;
    std::string     material;
    std::string     process;
    // The checkpoint link: the plate's manufacturing inputs when it left.
    std::string     manufacturing_input_hash;
    // The chat that was active when the G-code first left, and the item it
    // followed there.
    std::string     conversation_id;
    std::string     after_id;
    SliceStatistics statistics;
    std::vector<GcodeDestination> destinations;
    std::vector<GcodePrint>       prints;
};

// The entry's name: the latest exported file's name, else the plate's.
std::string gcode_display_name(const GcodeRecord& record);

// Stable lowercase SHA-256 for persisted provenance.
std::string sha256_hex(const std::string& bytes);
// The same over a file's bytes, read in chunks. Throws when the file cannot
// be read.
std::string sha256_file_hex(const std::string& path);

// The project keeps a copy of each G-code that left it, at
// <project dir>/gcode/<hash>.gcode, so it can be printed again without
// slicing. Only the most recently written copies stay; an older record's copy
// is gone and the entry says to slice again.
constexpr std::size_t kKeptGcodeCopies = 20;
// Copies `source` (whose bytes hash to `hash`) into the project, or marks an
// existing copy as just written, then removes the copies beyond `keep`.
// Returns the copy's path. Throws when the copy cannot be written.
std::string keep_gcode_copy(const std::string& project_dir, const std::string& hash, const std::string& source,
                            std::size_t keep = kKeptGcodeCopies);
// The kept copy's path, or empty when the project no longer keeps one.
std::string kept_gcode_copy(const std::string& project_dir, const std::string& hash);

// A Bambu printer takes a .gcode.3mf, not G-code: a send to one keeps the
// package OrcaSlicer uploaded, byte for byte, beside the G-code's copy as
// <hash>.gcode.3mf, with the config 3MF a cloud printer was also sent as
// <hash>.config.3mf. The newest send's package replaces an earlier one; it
// goes when its G-code's copy is pruned. Print again sends it as it is.
struct KeptPackage
{
    std::string package;
    std::string config; // empty when the send that left it was over LAN
};
// Keeps `package` (and `config`, when not empty) and returns the package's
// SHA-256. Throws when the G-code has no kept copy or a file cannot be written.
std::string keep_gcode_package(const std::string& project_dir, const std::string& hash, const std::string& package,
                               const std::string& config);
// The kept package, or nothing when the project keeps none for the G-code.
std::optional<KeptPackage> kept_gcode_package(const std::string& project_dir, const std::string& hash);
// The SHA-256 the kept package must have: the newest send's that kept one.
std::string kept_package_hash(const GcodeRecord& record);

// Canonical hash of the manufacturing facts exposed by IWorkspace for one
// plate. It deliberately excludes selection, undo state, dirty state, object
// names, and the process-local project session. A missing plate has no hash.
std::optional<std::string> manufacturing_input_hash(const Workspace::WorkspaceSnapshot& snapshot,
                                                    std::size_t                         plate_index);

// What a record says about one plate of `snapshot`: the plate, the presets in
// force, its input hash and its slice estimates. The G-code hash, the chat and
// the time are the caller's to add. Nothing for a plate the snapshot does not
// have.
std::optional<GcodeRecord> gcode_record(const Workspace::WorkspaceSnapshot& snapshot, std::size_t plate_index);

} // namespace Slic3r::GUI::JusPrin::Agent
