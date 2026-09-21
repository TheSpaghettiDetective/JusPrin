#pragma once

#include "ViewportToolStripLayout.hpp"

#include "slic3r/GUI/Gizmos/GLGizmosManager.hpp"

#include <map>
#include <string>

namespace Slic3r::GUI {
class GLCanvas3D;
class GizmoObjectManipulation;
}

namespace Slic3r::GUI::JusPrin {

class ShellTheme;
struct ShellPalette;

// The open tool's numbers, in a card beside the tool strip.
//
// Move, Rotate and Scale each get their own card, anchored to their own button
// and shown only while that tool is open. The three windows are ported from
// OrcaSlicer's own gizmo panels; see the note at the top of the .cpp for what
// the fork changed and what it deliberately left alone. Every value and every
// write still goes through GizmoObjectManipulation, so a drag on the 3D
// handles writes into the fields as it happens.
//
// A gizmo with no card of ours (Cut, Text, the painting tools, anything the
// Agent opens) keeps OrcaSlicer's own input window, anchored to the More
// button, since those panels are upstream's to draw.
class ToolValuePanel final
{
public:
    explicit ToolValuePanel(GLCanvas3D& canvas) : m_canvas(canvas) {}

    // Called from the strip's own render, inside the canvas's ImGui frame.
    void render(const ShellTheme& theme, const ShellPalette& palette, const StripLayout& layout, float scale);

    // Where each value field was last drawn, by the field id OrcaSlicer's
    // panels give it ("##rotation_z"). The integration harness clicks these
    // to type into a field the way a person does; nothing in the product
    // reads them. Empty until the card has drawn a frame.
    const std::map<std::string, StripRect>& field_rects() const { return m_field_rects; }

private:
    void render_move(const ShellTheme& theme, const ShellPalette& palette, GizmoObjectManipulation& manip, float scale);
    void render_rotate(const ShellTheme& theme, const ShellPalette& palette, GizmoObjectManipulation& manip, float scale);
    void render_scale(const ShellTheme& theme, const ShellPalette& palette, GizmoObjectManipulation& manip, float scale);

    void render_axis_captions(const ShellPalette& palette, float caption_max, float unit_size, float space_size,
                              int first_index = 1);
    bool send_focus_to_canvas(unsigned int active_id, const char* const labels[3], bool clear_when_unfocused = true);
    void render_footer(const ShellTheme& theme, const ShellPalette& palette, float scale);
    bool render_checkbox(const ShellTheme& theme, const ShellPalette& palette, const wxString& label, bool checked,
                         float scale);
    bool render_reset(const ShellTheme& theme, const ShellPalette& palette, const char* icon, const wxString& tooltip,
                      float scale);

    // OrcaSlicer's own numeric field, remembering where it landed.
    bool input_double(const ShellPalette& palette, const char* label, double* value);

    GLCanvas3D& m_canvas;
    // Upstream's rule for committing a field: the value is written when the
    // focused item changes, so this is the item that had focus last frame.
    unsigned int m_last_active_item{0};
    std::map<std::string, StripRect> m_field_rects;
};

} // namespace Slic3r::GUI::JusPrin
