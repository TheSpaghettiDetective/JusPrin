#pragma once

class wxBitmap;
class wxColour;

namespace Slic3r::GUI::JusPrin {

// One of the Lucide icons pinned in resources/jusprin/ui/icons, rasterized at
// size_px physical pixels in `color`, and cached per name, size and colour.
// Throws std::runtime_error when the icon is missing or cannot be parsed.
const wxBitmap& lucide_bitmap(const char* name, int size_px, const wxColour& color);

} // namespace Slic3r::GUI::JusPrin
