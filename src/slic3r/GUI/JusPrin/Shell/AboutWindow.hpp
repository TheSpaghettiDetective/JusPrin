#pragma once

#include <wx/dialog.h>
#include <wx/string.h>
#include <wx/timer.h>

class Button;
class wxSizer;

namespace Slic3r::GUI::JusPrin {

class ShellTheme;

// Help › About JusPrin (the "&About JusPrin" item in Help on Windows and
// Linux, and in the app menu on macOS) while the JusPrin view is on: the
// lockup, JusPrin's version, the OrcaSlicer release it is built on, the build
// commit, where to get help, and OrcaSlicer's own licences and credits window
// behind License…. Classic view keeps OrcaSlicer's About. Drawn on the
// "JusPrin v2" canvas, page "Splash & About", boards splash-about-04 and 05.
class AboutWindow : public wxDialog
{
public:
    AboutWindow(wxWindow* parent, const ShellTheme& theme);

    // What Copy version puts on the clipboard: the version a problem report
    // names, and the build.
    static wxString version_text();

private:
    wxSizer* build_version(wxWindow* parent);
    wxSizer* build_help(wxWindow* parent);
    wxSizer* build_license(wxWindow* parent);
    wxWindow* rule(wxWindow* parent);
    void      copy_version();
    void      show_copy_label(bool copied);

    const ShellTheme& m_theme;
    Button*           m_copy{nullptr};
    // How long Copy version reads "Copied".
    wxTimer           m_copied;
};

} // namespace Slic3r::GUI::JusPrin
