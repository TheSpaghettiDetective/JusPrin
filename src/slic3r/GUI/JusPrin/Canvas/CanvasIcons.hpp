#pragma once

#include <string>

namespace Slic3r::GUI {
class GLCanvas3D;
}

namespace Slic3r::GUI::JusPrin {

// How many of ImGui's pixels one design-token pixel is on this canvas. Not
// ImGuiWrapper::get_style_scaling(): that comes from wx's content scale factor,
// which stays 1 on Windows at any display scale while the canvas's font grows,
// so a token-sized box stops fitting its text above 100%. On macOS and GTK 3
// the two are the same number.
float canvas_ui_scale(const GLCanvas3D& canvas);

// The shared icon textures for everything the fork draws inside the GL canvas.
//
// Each icon is one of the currentColor SVGs in resources/jusprin/ui/icons,
// rasterized white on transparent at the size asked for, so ImGui's tint gives
// it any token colour. Textures are cached per name and size and rebuilt when
// the display scale changes. Call it with a GL context current; it throws when
// an icon is missing from the packaged resources, which is a packaging fault
// rather than something to draw around.
//
// Returns the GL texture id, ready for ImDrawList::AddImage.
unsigned int canvas_icon_texture(const std::string& name, int size_px);

} // namespace Slic3r::GUI::JusPrin
