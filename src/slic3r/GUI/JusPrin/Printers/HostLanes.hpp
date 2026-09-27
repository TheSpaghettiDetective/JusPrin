#pragma once

// What a Klipper printer's multi-filament unit reports as loaded, read from
// its Moonraker host, and how fresh that reading is. No wx and no Orca types:
// the parsing and the freshness rules are testable without a GUI or a
// printer. HostLaneReader does the network half and hands readings in here.
//
// The two sources and what they mean mirror OrcaSlicer's own
// MoonrakerPrinterAgent (fetch_moonraker_filament_data and
// fetch_hh_filament_info). That agent cannot be called for this: its readers
// are private, run on the calling thread, and write the shared device state;
// and its status stream is compiled out, so the device it feeds never counts
// as live. When upstream changes what those two functions accept, change the
// parsers here to match.

#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Printers {

// One lane that holds filament. `colour` is "#RRGGBB", or empty when the lane
// reported none or one that is not a colour: a missing colour is not white.
struct HostLane
{
    int         index{0}; // the lane's tool number, from 0
    std::string material; // as the unit names it, such as "PLA"
    std::string colour;
};

// A reply read as one of the two sources. `unit` is false when the source
// shows no multi-filament unit; `loaded` then is empty.
struct UnitReport
{
    bool                  unit{false};
    std::vector<HostLane> loaded; // in lane order
};

// "#RRGGBB" for "#RRGGBB", "RRGGBB", "0xRRGGBB" or "RRGGBBAA"; empty otherwise.
std::string lane_colour(const std::string& raw);

// The body of GET /server/database/item?namespace=lane_data (AFC, and Happy
// Hare since February 2026). A unit is there when any entry has a lane
// number; a lane holds filament when it names a material. Nothing when the
// body is not that reply.
std::optional<UnitReport> parse_lane_data(const std::string& body);

// The body of GET /printer/objects/query?mmu (Happy Hare). A unit is there
// when `mmu` names a positive gate count; a gate holds filament when its
// status is above zero and it names a material. Nothing when the body is not
// that reply.
std::optional<UnitReport> parse_happy_hare(const std::string& body);

// What one attempt to read a host found.
struct HostReading
{
    // The host answered as Moonraker does.
    bool reached{false};
    // What is loaded, when the host said: empty for a unit with nothing loaded
    // and for a printer with no unit alike -- both are the printer saying
    // there is nothing to show. Absent when the host did not say.
    std::optional<std::vector<HostLane>> loaded;
};

enum class HostLink { Checking, Reachable, Unreachable };

struct HostStatus
{
    HostLink                             link{HostLink::Checking};
    // Only while Reachable, and only when the host said.
    std::optional<std::vector<HostLane>> loaded;
};

// A reading counts for as long as a Bambu printer's pushed data does
// (PrinterSetup::has_recent_printer_data): three missed polls.
inline constexpr std::chrono::seconds kHostReadingFresh{30};
inline constexpr std::chrono::seconds kHostPollInterval{10};

// The latest reading of each host, keyed by whatever identifies the host's
// settings: a changed address or key is a different host. Thread-safe: the
// readings arrive on worker threads.
class HostReadings
{
public:
    using Clock = std::chrono::steady_clock;

    // Reachable while an answer is fresh; Unreachable while the last attempt,
    // itself fresh, failed with no fresh answer; Checking otherwise -- never
    // asked, or asked too long ago to say.
    HostStatus status(const std::string& key, Clock::time_point now) const;

    // True, and the attempt recorded as started, when `key` is due another
    // read: none in flight and none started in the last poll interval.
    bool begin(const std::string& key, Clock::time_point now);
    void finish(const std::string& key, HostReading reading, Clock::time_point now);

    // The host answered some other request, such as a connection test. It
    // says nothing about what is loaded.
    void note_answered(const std::string& key, Clock::time_point now);

private:
    struct Entry
    {
        std::optional<Clock::time_point>     started;
        bool                                 in_flight{false};
        std::optional<Clock::time_point>     answered;  // last time the host answered
        std::optional<Clock::time_point>     failed;    // last attempt that got no answer
        std::optional<std::vector<HostLane>> loaded;    // from the last answer
    };

    mutable std::mutex           m_mutex;
    std::map<std::string, Entry> m_entries;
};

} // namespace Slic3r::GUI::JusPrin::Printers
