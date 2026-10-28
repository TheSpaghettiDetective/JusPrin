#pragma once

// Whether the selected printer profile is one the person set up. GUI-free so
// the rule is testable on its own.

#include "libslic3r/Preset.hpp"

namespace Slic3r::GUI::JusPrin::Printers {

// A visible user printer profile. Profiles embedded in a project, shipped in
// a bundle, or supplied by a vendor are not printers the person added.
bool is_named_printer(const Preset& preset);

// The printer profile came with something other than the person's own setup:
// a system profile a 3MF named, as OrcaSlicer selects when it opens a file
// saved for another printer. OrcaSlicer's placeholder is no printer at all,
// so it is not marked. Nothing is stored; the header asks on every refresh.
bool printer_from_file(const Preset& preset);

} // namespace Slic3r::GUI::JusPrin::Printers
