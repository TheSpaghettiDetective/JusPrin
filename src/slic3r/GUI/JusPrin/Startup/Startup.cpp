#include "Startup.hpp"

#include "SplashWindow.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/JusPrin/Brand/BrandPalette.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellController.hpp"
#include "slic3r/GUI/MainFrame.hpp"

#include <boost/log/trivial.hpp>

#include <wx/gdicmn.h>
#include <wx/weakref.h>

namespace Slic3r::GUI::JusPrin {

namespace {

// Weak, as OrcaSlicer holds its own: whatever destroys the window clears it.
wxWeakRef<SplashWindow>& splash()
{
    static wxWeakRef<SplashWindow> window;
    return window;
}

} // namespace

bool startup_begin(GUI_App& app)
{
    // OrcaSlicer's own preference to show no splash holds for JusPrin's too.
    if (!app.is_editor() || classic_view_chosen() || app.app_config == nullptr ||
        app.app_config->get("show_splash_screen") != "true")
        return false;
    const ShellTheme* theme = brand_theme();
    if (theme == nullptr)
        return false;

    // On the display where the main window last stood, as OrcaSlicer's splash.
    wxPoint position = wxDefaultPosition;
    if (app.app_config->has("window_mainframe"))
        if (auto metrics = WindowMetrics::deserialize(app.app_config->get("window_mainframe")))
            position = metrics->get_rect().GetPosition();

    BOOST_LOG_TRIVIAL(info) << "begin to show the JusPrin splash screen...";
    splash() = new SplashWindow(*theme, position);
    splash()->present();
    return true;
}

void close_splash_when_shown(MainFrame& frame)
{
    if (splash())
        splash()->close_when_shown(frame);
}

} // namespace Slic3r::GUI::JusPrin
