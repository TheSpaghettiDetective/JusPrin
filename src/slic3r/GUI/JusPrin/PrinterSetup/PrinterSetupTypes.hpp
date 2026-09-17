#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

// What a printer is doing, as far as this application can see. Offline is a
// statement about what was observed, not a claim that the machine is off.
enum class PrinterActivity : std::uint8_t { Offline, Idle, Printing };

struct DiscoveredPrinter
{
    std::string stable_id;
    std::string name;
    std::string ip_address;
    std::string device_model_id;
    std::string connection;
    bool connected{false};
    PrinterActivity activity{PrinterActivity::Offline};
    // When the device last said anything, as milliseconds since the epoch.
    // Zero when it never has: a printer known from a previous session but not
    // heard from since is a real state, and a fabricated timestamp is not.
    std::int64_t observed_at_ms{0};
    // What the printer reported about itself; zero or empty when it has not said.
    double nozzle_diameter{0.};
    std::string ams_name;
    struct Spool
    {
        std::string name;
        std::string colour;   // "#RRGGBB"
        std::string material; // the filament type, such as PLA; may be empty
    };
    std::vector<Spool> spools;
    // The machine the app is working with, as Orca's device manager has it.
    bool selected{false};
    // The job in progress; -1 and empty when it has not said.
    int         progress_percent{-1};
    std::string job;
    // Degrees Celsius, as last reported; absent when it has not said.
    std::optional<double> nozzle_temperature;
    std::optional<double> bed_temperature;
};

struct PrinterCandidate
{
    // Opaque, session-local identifier for one model and nozzle variant.
    std::string id;
    std::string vendor_id;
    std::string vendor_name;
    std::string model_id;
    std::string model_name;
    std::string device_model_id;
    std::string variant;
    std::string preset_name;
    std::string build_volume;
    std::string default_material;
    std::string default_plate;
    std::string artwork_path;
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
