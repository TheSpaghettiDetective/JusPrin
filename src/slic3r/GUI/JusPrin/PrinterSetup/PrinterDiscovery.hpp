#pragma once

#include "PrinterSetupTypes.hpp"

#include <vector>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

// Snapshots device fields on the GUI thread. No MachineObject pointer crosses
// the boundary into the dialog/controller.
//
// Printers the app has not heard from are left out by default, because setup
// offers what can be connected to now. A caller reporting what the app knows,
// rather than what it can offer, asks for them.
std::vector<DiscoveredPrinter> discover_printers(bool include_unreachable = false);

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
