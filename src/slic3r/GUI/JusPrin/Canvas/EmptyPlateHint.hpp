#pragma once

namespace Slic3r::GUI {
class GLCanvas3D;
}

namespace Slic3r::GUI::JusPrin {

// The line an empty Prepare canvas shows at its centre, "Drop a model here or
// add one": a drop on the canvas adds to the project, and Add model… in the
// Plates pane does the same (architecture.md, "Opening and importing files").
// Drawn with ImGui inside the canvas's own frame, as the print-issue pill is;
// it takes no pointer, so a drop or a click reaches the canvas beneath it.
// The caller draws it only while the project has no objects.
void render_empty_plate_hint(GLCanvas3D& canvas);

} // namespace Slic3r::GUI::JusPrin
