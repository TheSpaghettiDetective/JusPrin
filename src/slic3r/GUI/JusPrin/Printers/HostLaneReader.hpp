#pragma once

// Reads what a named printer's Moonraker host says its multi-filament unit
// holds, off the UI thread, and keeps the latest reading for Home's printer
// cards and the printer conversation. Only printers connected as a Moonraker
// print host are read; the printer agent Orca has selected is not involved,
// so a Bambu printer's live connection is left as it is.
//
// Nothing here blocks: a caller gets the reading as it stands and, when one
// is due, starts the next. Readings are asked for only while something shows
// them, so a host is not polled while Home is off screen.

#include "HostLanes.hpp"

#include <optional>
#include <string>

namespace Slic3r::GUI::JusPrin::Printers {

// The latest reading of the named printer `name`, starting another when one
// is due. Nothing when that printer has no Moonraker host.
std::optional<HostStatus> host_status(const std::string& name);

// Starts every due read, for all named printers with a Moonraker host.
void refresh_host_readings();

// The named printer's host has just answered a connection test.
void note_host_answered(const std::string& name);

} // namespace Slic3r::GUI::JusPrin::Printers
