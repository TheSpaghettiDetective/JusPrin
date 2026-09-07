#pragma once

// Remembered spools, a fork-owned concept OrcaSlicer does not have.
//
// Orca knows filament presets and one colour per extruder in the project
// config. It has no idea that the same PLA+ preset in white and in black are
// two physical reels a person swaps between. A spool is that pairing, named by
// the person who owns it, remembered per printer.
//
// Machine facts are app-level, not project-level, so this lives beside the
// application data rather than inside the 3mf: a project opened on another
// machine describes the same print, not the same shelf of filament.
//
// GUI-free and Orca-free: the store holds preset *names* and hex strings, so
// it can be tested without wx, a PresetBundle, or a data directory. Selecting
// a spool -- which does touch Orca -- belongs to Shell/SetupCommands.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Workspace {

struct Spool
{
    std::string id;              // opaque, unique within the store
    std::string printer_preset;  // Orca printer preset this spool belongs to
    std::string filament_preset; // Orca filament preset name
    std::string colour;          // "#RRGGBB"
    std::string name;            // user-visible and editable
    std::string brand;           // vendor cached from the preset at creation
    std::string last_used;       // ISO-8601 UTC

    bool operator==(const Spool& rhs) const
    {
        return id == rhs.id && printer_preset == rhs.printer_preset && filament_preset == rhs.filament_preset &&
               colour == rhs.colour && name == rhs.name && brand == rhs.brand && last_used == rhs.last_used;
    }
};

// A JSON file of spools, loaded once at construction and written through on
// every mutation. Small by design: a person keeps three to fifteen spools, so
// there is no index, no cache, and no partial write.
class SpoolStore
{
public:
    struct Config
    {
        // Absolute path of the JSON file. Required.
        std::string file_path;
        // Injectable for deterministic tests.
        std::function<std::string()> clock; // ISO-8601 UTC timestamp
        std::function<std::string()> uuid;  // opaque unique identifier
    };

    // Reads the file. A missing file is an empty store, which is the ordinary
    // first-run state.
    //
    // A file that exists but does not parse is moved aside to
    // "<file_path>.corrupt" and the store starts empty. Overwriting it would
    // destroy spools the person named by hand, and refusing to start would
    // take the chip down over a file the product can rebuild by seeding. The
    // move is reported through corrupt(), so the state stays visible rather
    // than being logged and forgotten.
    explicit SpoolStore(Config config);

    SpoolStore(const SpoolStore&) = delete;
    SpoolStore& operator=(const SpoolStore&) = delete;

    // True when the backing file existed, could not be parsed, and was moved
    // aside by this constructor.
    bool               corrupt() const { return m_corrupt; }
    const std::string& corrupt_reason() const { return m_corrupt_reason; }

    // Every spool for this printer, most recently used first. Ties break on
    // id so the order is stable across runs.
    std::vector<Spool> spools_for(const std::string& printer_preset) const;

    // The spool whose filament preset and colour both match what the project
    // currently has loaded, or nothing when the project does not correspond to
    // a remembered spool.
    std::optional<Spool> current(const std::string& printer_preset,
                                 const std::string& filament_preset,
                                 const std::string& colour) const;

    std::optional<Spool> find(const std::string& id) const;

    // Assigns id and last_used, stores, and returns the stored record.
    Spool add(Spool spool);

    // Each returns false when the id is unknown; the caller owns the decision
    // about a stale menu, so an unknown id is a normal answer here rather than
    // an error.
    bool rename(const std::string& id, const std::string& name);
    bool recolour(const std::string& id, const std::string& colour);
    bool remove(const std::string& id);
    bool touch(const std::string& id);

    std::size_t size() const { return m_spools.size(); }

private:
    // Write temp, rename over. Throws when the write fails: a spool the person
    // just named must not silently fail to persist.
    void write();

    Config             m_config;
    std::vector<Spool> m_spools;
    bool               m_corrupt{false};
    std::string        m_corrupt_reason;
};

} // namespace Slic3r::GUI::JusPrin::Workspace
