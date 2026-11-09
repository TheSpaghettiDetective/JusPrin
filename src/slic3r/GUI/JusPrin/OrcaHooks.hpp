#pragma once

// The one JusPrin header OrcaSlicer's own files include to call into the
// fork. Each call from an upstream file is one line marked "// JusPrin:", so a
// rebase onto a new OrcaSlicer release meets as few changed lines as possible;
// what a call does lives in the fork's own files under src/slic3r/GUI/JusPrin.
// See agent-docs/jusprin/fork-stewardship.md.

#include "Brand/BrandPalette.hpp" // IWYU pragma: export
#include "Startup/Startup.hpp"    // IWYU pragma: export
