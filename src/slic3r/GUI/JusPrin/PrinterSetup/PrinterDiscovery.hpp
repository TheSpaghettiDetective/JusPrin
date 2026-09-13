#pragma once

#include "PrinterSetupTypes.hpp"

#include <vector>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

// Snapshots device fields on the GUI thread. No MachineObject pointer crosses
// the boundary into the dialog/controller.
std::vector<DiscoveredPrinter> discover_printers();

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
