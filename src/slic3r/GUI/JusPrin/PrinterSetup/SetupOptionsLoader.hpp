#pragma once

#include "PrinterBackend.hpp"

#include <string>
#include <vector>

namespace Slic3r { class Preset; }

namespace Slic3r::GUI::JusPrin::PrinterSetup {

// What the printer form offers for one machine profile, read from a folder of
// packaged profiles the way OrcaSlicer loads its system presets: the plates it
// offers and the one it starts on, and the filament presets it counts as
// compatible, those it does not, and how `own_presets` (copies of the
// person's own filament presets) fare against the printer. It builds its own
// preset bundle, so the person's installed presets are neither read nor
// changed, and it is safe off the GUI thread.
//
// A profiles folder that cannot be read is reported in `error`; a machine
// profile the folder does not hold is a caller's bug and throws.
SetupOptions load_setup_options(const std::string& profiles_directory, const std::string& vendor_id,
                                const std::string& printer_preset, const std::vector<Preset>& own_presets = {});

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
