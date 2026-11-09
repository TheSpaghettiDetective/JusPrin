// First: slic3r/GUI/I18N.hpp defines _L only while the _ macro is still
// undefined, so it has to precede any header that brings one in.
#include "slic3r/GUI/I18N.hpp"

#include "SplashWindow.hpp"

#include "slic3r/GUI/BitmapCache.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"
#include "slic3r/GUI/JusPrin/Updates/Version.hpp"
#include "libslic3r/Utils.hpp"

#ifdef __APPLE__
#include "SplashBar.hpp"
#endif

#ifdef __WXGTK__
#include "slic3r/GUI/LinuxDisplayBackend.hpp"
#include <gtk/gtk.h>
#endif

#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>

#include <wx/app.h>
#include <wx/dcclient.h>
#include <wx/event.h>
#include <wx/frame.h>
#include <wx/gdicmn.h>
#include <wx/string.h>
#include <wx/toplevel.h>

namespace Slic3r::GUI::JusPrin {

namespace {

long splash_style()
{
#ifdef __APPLE__
    return wxBORDER_NONE | wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP;
#else
    return wxBORDER_NONE | wxFRAME_NO_TASKBAR;
#endif
}

// Where the name animation plays, over the lockup's name, in window pixels.
wxRect name_box(const wxWindow& window, const SplashMetrics& s)
{
    return wxRect(window.FromDIP(wxPoint(s.name_left, s.name_top)), window.FromDIP(wxSize(s.name_width, s.name_height)));
}

} // namespace

SplashWindow::SplashWindow(const ShellTheme& theme, const wxPoint& pos)
    // wxSplashScreen sizes itself to the bitmap; it is blank because the
    // window paints itself. No wxSPLASH_TIMEOUT: the splash stays until the
    // main window is shown.
    : wxSplashScreen(wxBitmap(FromDIP(wxSize(theme.metrics().splash.size, theme.metrics().splash.size), nullptr)),
                     wxSPLASH_CENTRE_ON_SCREEN, 0, nullptr, wxID_ANY, wxDefaultPosition, wxDefaultSize, splash_style())
    , m_theme(theme)
{
    SetName("JusPrin splash");
#if defined(__WXGTK__)
    // Some desktops show a title bar on a splash under Wayland; as OrcaSlicer's
    // splash does, give the window an empty client-side title bar and then no
    // decoration.
    if (is_running_on_wayland()) {
        GtkWidget* empty = gtk_fixed_new();
        gtk_widget_set_size_request(empty, 0, 0);
        gtk_window_set_titlebar(GTK_WINDOW(GetHandle()), empty);
        gtk_window_set_decorated(GTK_WINDOW(GetHandle()), false);
    }
#endif
    SetPosition(pos);
    CenterOnScreen();

    const wxSize size = m_window->GetClientSize();
    BitmapCache  cache;
    if (const wxBitmap* lockup = cache.load_svg(GUI_App::dark_mode() ? "splash_logo_dark" : "splash_logo", size.GetWidth(),
                                                size.GetHeight()))
        m_lockup = *lockup;

    m_window->Bind(wxEVT_PAINT, &SplashWindow::on_paint, this);
}

void SplashWindow::present()
{
#ifdef __APPLE__
    // The animation draws the name, so the paint below leaves it out.
    m_plays_name = true;
#endif
    Show();
    // Painted now: once this returns, startup blocks the main thread until
    // the main window shows, and nothing paints in between.
    m_window->Refresh();
    m_window->Update();
#ifdef __APPLE__
    // Added after the paint, so the one commit that sends the moving bar to
    // the window server carries the painted splash with it.
    const SplashMetrics& s       = m_theme.metrics().splash;
    const ShellPalette&  palette = m_theme.palette(GUI_App::dark_mode());
    const wxRect track((m_window->GetClientSize().GetWidth() - FromDIP(s.bar_width)) / 2, FromDIP(s.bar_top),
                       FromDIP(s.bar_width), FromDIP(m_theme.metrics().progress_height));
    // The name first: the paint above leaves it out, and this commit puts it
    // back on screen together with the paint.
    const auto animation = boost::filesystem::path(resources_dir()) / "jusprin" / "startup" /
                           (GUI_App::dark_mode() ? "splash-name-dark.webp" : "splash-name-light.webp");
    add_splash_animation(*m_window, name_box(*this, s), animation.string());
    add_splash_bar(*m_window, track, FromDIP(s.bar_segment_width), s.bar_sweep_ms, palette.border_subtle,
                   palette.action_primary);
#endif
    wxYield();
}

void SplashWindow::close_when_shown(wxWindow& window) { m_close_with = &window; }

int SplashWindow::FilterEvent(wxEvent& event)
{
    if (m_close_with != nullptr && event.GetEventType() == wxEVT_SHOW && event.GetEventObject() == m_close_with &&
        static_cast<wxShowEvent&>(event).IsShown()) {
        m_close_with = nullptr;
        BOOST_LOG_TRIVIAL(info) << "JusPrin splash screen closed";
        // Gone from the screen now; destroyed on the event loop's next turn,
        // because this filter is still being called from the filter list.
        Hide();
        CallAfter([this] { Destroy(); });
    }
    return wxEventFilter::Event_Skip;
}

void SplashWindow::on_paint(wxPaintEvent&)
{
    wxPaintDC           dc(m_window);
    const ShellPalette& palette = m_theme.palette(GUI_App::dark_mode());
    const wxSize        size    = m_window->GetClientSize();

    dc.SetBackground(wxBrush(palette.surface_canvas));
    dc.Clear();
    if (m_lockup.IsOk())
        dc.DrawBitmap(m_lockup, 0, 0, true);
    if (m_plays_name) {
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(palette.surface_canvas));
        dc.DrawRectangle(name_box(*this, m_theme.metrics().splash));
    }

    // The version line's box starts at version_top, the text centred in it.
    dc.SetFont(m_theme.font(TextRole::BodySmall));
    dc.SetTextForeground(palette.text_secondary);
    const wxRect line(0, FromDIP(m_theme.metrics().splash.version_top), size.GetWidth(),
                      FromDIP(m_theme.type_style(TextRole::BodySmall).line_height));
    dc.DrawLabel(wxString::Format(_L("Version %s"), Updates::kVersion), line, wxALIGN_CENTER);

    // The 1 DIP edge that keeps the square apart from what is behind it.
    dc.SetPen(wxPen(palette.border_subtle, FromDIP(1)));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(wxRect(size));
}

} // namespace Slic3r::GUI::JusPrin
