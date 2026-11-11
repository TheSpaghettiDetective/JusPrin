// First: slic3r/GUI/I18N.hpp defines _L only while the _ macro is still
// undefined, so it has to precede any header that brings one in.
#include "slic3r/GUI/I18N.hpp"

#include "SplashWindow.hpp"

#include "slic3r/GUI/BitmapCache.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"
#include "slic3r/GUI/JusPrin/Updates/Version.hpp"

#if defined(__APPLE__) || defined(_WIN32)
#include "NameAnimation.hpp"
#include "SplashAnimation.hpp"
#endif

#ifdef __WXGTK__
#include "slic3r/GUI/LinuxDisplayBackend.hpp"
#include <gtk/gtk.h>
#endif

#include <boost/log/trivial.hpp>

#include <wx/app.h>
#include <wx/brush.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/event.h>
#include <wx/frame.h>
#include <wx/gdicmn.h>
#include <wx/string.h>
#include <wx/toplevel.h>
#include <wx/window.h>

#include <utility>

#if defined(__APPLE__) || defined(_WIN32)
#include <wx/image.h>
#include <wx/imagwebp.h>

#include <vector>
#endif

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

wxSize splash_size(const ShellTheme& theme)
{
    return wxWindow::FromDIP(wxSize(theme.metrics().splash.size, theme.metrics().splash.size), nullptr);
}

// What the window shows from wxSplashScreen's constructor until present()
// paints it: the splash's background, where a bitmap left as it is shows black.
wxBitmap plain(const ShellTheme& theme)
{
    wxBitmap   bitmap(splash_size(theme));
    wxMemoryDC dc(bitmap);
    dc.SetBackground(wxBrush(theme.palette(GUI_App::dark_mode()).surface_canvas));
    dc.Clear();
    dc.SelectObject(wxNullBitmap);
    return bitmap;
}

// Where the name animation plays, over the lockup's name, in window pixels.
wxRect name_box(const wxWindow& window, const SplashMetrics& s)
{
    return wxRect(window.FromDIP(wxPoint(s.name_left, s.name_top)), window.FromDIP(wxSize(s.name_width, s.name_height)));
}

} // namespace

SplashWindow::Art SplashWindow::make_art(const ShellTheme& theme)
{
    Art          art;
    const wxSize size = splash_size(theme);
    BitmapCache  cache;
    if (const wxBitmap* lockup = cache.load_svg(GUI_App::dark_mode() ? "splash_logo_dark" : "splash_logo", size.GetWidth(),
                                                size.GetHeight()))
        art.lockup = *lockup;
#if defined(__APPLE__) || defined(_WIN32)
    art.name = read_name_animation(NameAnimation::Splash);
#endif
    return art;
}

SplashWindow::SplashWindow(const ShellTheme& theme, const wxPoint& pos) : SplashWindow(theme, pos, make_art(theme)) {}

SplashWindow::SplashWindow(const ShellTheme& theme, const wxPoint& pos, Art art)
    // wxSplashScreen sizes itself to the bitmap and shows it at once; the
    // window paints itself from present() on. No wxSPLASH_TIMEOUT: the splash
    // stays until the main window is shown.
    : wxSplashScreen(plain(theme), wxSPLASH_CENTRE_ON_SCREEN, 0, nullptr, wxID_ANY, wxDefaultPosition, wxDefaultSize, splash_style())
    , m_theme(theme)
    , m_art(std::move(art))
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

    // The Windows animation draws its frames over this colour.
    m_window->SetBackgroundColour(theme.palette(GUI_App::dark_mode()).surface_canvas);
    m_window->Bind(wxEVT_PAINT, &SplashWindow::on_paint, this);
}

void SplashWindow::present()
{
#if defined(__APPLE__) || defined(_WIN32)
    // The animation draws the name, so the paint below leaves it out.
    m_plays_name = true;
    std::vector<wxWebPAnimationFrame> frames = std::move(m_art.name);
    const wxRect                      box    = name_box(*this, m_theme.metrics().splash);
#ifdef _WIN32
    // The animation's window takes some milliseconds to open over the splash,
    // so the paint puts its first frame there, scaled as the window scales it.
    m_name_first = wxBitmap(frames.front().image.Scale(box.width, box.height, wxIMAGE_QUALITY_HIGH));
#endif
#endif
    Show();
    // Painted now: once this returns, startup blocks the main thread until
    // the main window shows, and nothing paints in between.
    m_window->Refresh();
    m_window->Update();
#if defined(__APPLE__) || defined(_WIN32)
    // Added after the paint, which leaves the name out: on the Mac the one
    // commit that sends the animation to the window server carries the
    // painted splash with it, and on Windows the animation's window opens
    // over the painted splash.
    add_splash_animation(*m_window, box, std::move(frames));
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
    if (m_art.lockup.IsOk())
        dc.DrawBitmap(m_art.lockup, 0, 0, true);
    if (m_plays_name) {
        const wxRect box = name_box(*this, m_theme.metrics().splash);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(palette.surface_canvas));
        dc.DrawRectangle(box);
        if (m_name_first.IsOk())
            dc.DrawBitmap(m_name_first, box.GetTopLeft(), true);
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
