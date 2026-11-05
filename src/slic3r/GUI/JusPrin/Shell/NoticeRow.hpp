#pragma once

// One row across the top of a window: a sentence on the left and the one
// thing to do about it on the right. Classic view's way back and the printer
// monitor's "stopped updating" line are both one.

#include <wx/panel.h>

#include <functional>

class Button;
class wxStaticText;

namespace Slic3r::GUI::JusPrin {

class ShellTheme;

class NoticeRow : public wxPanel
{
public:
    // `warning` puts the warning colour's status dot before the sentence.
    NoticeRow(wxWindow* parent, const ShellTheme& theme, const wxString& text, const wxString& action,
              std::function<void()> on_action, bool warning = false);

    Button* button() const { return m_button; }

    void apply_appearance(bool dark);

private:
    const ShellTheme& m_theme;
    wxStaticText*     m_text{nullptr};
    Button*           m_button{nullptr};
    bool              m_warning{false};
    bool              m_dark{false};
};

} // namespace Slic3r::GUI::JusPrin
