#pragma once

#include <wx/bitmap.h>
#include <wx/gdicmn.h>
#include <wx/event.h>
#include <wx/splash.h>

#if defined(__APPLE__) || defined(_WIN32)
#include <wx/imagwebp.h>

#include <vector>
#endif

class wxPaintEvent;

namespace Slic3r::GUI::JusPrin {

class ShellTheme;

// JusPrin's startup splash while the JusPrin view is on: the stacked lockup
// and the version, and on macOS and Windows the nozzle printing the name in a
// loop while the app loads (resources/jusprin/startup, drawn by
// Brand/splash_animation.py). It has no steps or percentage: the main thread
// is blocked through most of startup, so only motion that runs off it can
// show (add_splash_animation); Linux shows the still lockup. Drawn on the
// "JusPrin v2" canvas, page "Splash & About", boards splash-about-01 to 03.
// Stays open until it is destroyed (close_splash_when_shown).
class SplashWindow : public wxSplashScreen
{
public:
    // pos is where the main window last stood; the splash centres on that
    // display.
    SplashWindow(const ShellTheme& theme, const wxPoint& pos);

    // Shows and paints the splash, and on macOS and Windows starts the
    // animation, before startup blocks the main thread.
    void present();

    // Closes the splash when window is first shown.
    void close_when_shown(wxWindow& window);

    // wxSplashScreen sees every event in the app first, and closes itself on
    // any key or click; startup holds the splash across the whole load, so it
    // ignores them (as OrcaSlicer's splash does, for the same use-after-free).
    // It watches here for the main window's first show instead: a handler
    // bound on MainFrame would not hear it, because MainFrame's own wxEVT_SHOW
    // handler is bound after the shell attaches and does not pass it on.
    int FilterEvent(wxEvent& event) override;

private:
    // What is slow to make, made before wxSplashScreen's constructor shows the
    // window: until present() paints, the window shows the constructor's
    // plain bitmap, and anything made in between would keep it plain.
    struct Art
    {
        wxBitmap lockup;
#if defined(__APPLE__) || defined(_WIN32)
        std::vector<wxWebPAnimationFrame> name; // given to the animation in present()
#endif
    };
    static Art make_art(const ShellTheme& theme);
    SplashWindow(const ShellTheme& theme, const wxPoint& pos, Art art);

    void on_paint(wxPaintEvent& event);

    const ShellTheme& m_theme;
    Art               m_art;
    wxWindow*         m_close_with{nullptr};
    // The animation draws the name, so the paint leaves it out: on the Mac
    // the box is left blank; on Windows it shows the animation's first frame
    // (m_name_first), which the animation's window then covers.
    bool              m_plays_name{false};
    wxBitmap          m_name_first;
};

} // namespace Slic3r::GUI::JusPrin
