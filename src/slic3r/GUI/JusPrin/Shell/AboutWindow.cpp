// First: slic3r/GUI/I18N.hpp defines _L only while the _ macro is still
// undefined, so it has to precede any header that brings one in.
#include "slic3r/GUI/I18N.hpp"

#include "AboutWindow.hpp"

#include "LucideIcon.hpp"
#include "ShellRecipes.hpp"
#include "ShellTheme.hpp"

#include "slic3r/GUI/AboutDialog.hpp"
#include "slic3r/GUI/BuildCommit.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Startup/NameAnimation.hpp"
#include "slic3r/GUI/JusPrin/Updates/Version.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include "libslic3r_version.h"

#include <wx/bitmap.h>
#include <wx/brush.h>
#include <wx/clipbrd.h>
#include <wx/colour.h>
#include <wx/dataobj.h>
#include <wx/dcbuffer.h>
#include <wx/dialog.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/image.h>
#include <wx/imagwebp.h>
#include <wx/panel.h>
#include <wx/pen.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/timer.h>
#include <wx/utils.h>
#include <wx/window.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Slic3r::GUI::JusPrin {

namespace {

constexpr char kSupportEmail[] = "jusprin-support@obico.io";
constexpr char kDiscordInvite[] = "discord.gg/Tx67dHNYH3";
constexpr char kRepository[]    = "github.com/TheSpaghettiDetective/JusPrin";
// The Lucide glyph in a secondary button, as the design system draws it.
constexpr int  kButtonIconDip   = 16;
// The gap between that glyph and the button's label on the board.
constexpr int  kButtonIconGapDip = 6;

// The 1 DIP line between sections. A plain window, as a wxPanel with no
// focusable children would take a Tab stop of its own.
class Rule : public wxWindow
{
public:
    using wxWindow::wxWindow;
    bool AcceptsFocus() const override { return false; }
};

// The overlay's horizontal lockup, in the ink of this mode, with the nozzle
// printing the name once and stopping on the finished logo: the About version
// of the name animation, scaled to this lockup's name in the box
// component.about.name*. Unlike at startup the main thread is free here, so a
// timer plays it on every platform.
class AnimatedLockup : public wxWindow
{
public:
    AnimatedLockup(wxWindow* parent, const ShellTheme& theme, const wxColour& background)
        : wxWindow(parent, wxID_ANY), m_background(background), m_timer(this)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        const AboutMetrics& about = theme.metrics().about;
        m_lockup = ScalableBitmap(this, GUI_App::dark_mode() ? "OrcaSlicer_horizontal_dark" : "OrcaSlicer_horizontal_light",
                                  about.lockup_height)
                       .bmp();
        m_box = wxRect(FromDIP(wxPoint(about.name_left, about.name_top)), FromDIP(wxSize(about.name_width, about.name_height)));

        // Scaled once, to the box at the screen's pixels.
        const double scale = GetContentScaleFactor();
        const wxSize pixels(std::lround(m_box.width * scale), std::lround(m_box.height * scale));
        for (wxWebPAnimationFrame& frame : read_name_animation(NameAnimation::About)) {
            wxBitmap bitmap(frame.image.Scale(pixels.x, pixels.y, wxIMAGE_QUALITY_HIGH));
            bitmap.SetScaleFactor(scale);
            m_frames.emplace_back(std::move(bitmap), frame.duration);
        }

        // The carriage swings a little past the lockup's right edge.
        const wxSize lockup = m_lockup.GetLogicalSize();
        SetMinSize(wxSize(std::max(lockup.x, m_box.GetRight() + 1), std::max(lockup.y, m_box.GetBottom() + 1)));
        Bind(wxEVT_PAINT, &AnimatedLockup::on_paint, this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
            ++m_current;
            Refresh(false);
            if (m_current + 1 < m_frames.size())
                m_timer.StartOnce(m_frames[m_current].second);
        });
        if (m_current + 1 < m_frames.size())
            m_timer.StartOnce(m_frames[m_current].second);
    }

private:
    void on_paint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(m_background));
        dc.Clear();
        dc.DrawBitmap(m_lockup, 0, 0, true);
        // The animation draws the name, so the lockup's own is covered.
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(m_background));
        dc.DrawRectangle(m_box);
        dc.DrawBitmap(m_frames[m_current].first, m_box.GetTopLeft(), true);
    }

    wxColour                              m_background;
    wxBitmap                              m_lockup;
    wxRect                                m_box;
    // Each frame and how long it shows, in milliseconds.
    std::vector<std::pair<wxBitmap, int>> m_frames;
    size_t                                m_current{0};
    wxTimer                               m_timer;
};

void open_link(wxWindow* parent, const wxString& url)
{
    if (!wxLaunchDefaultBrowser(url))
        MessageDialog(parent, _L("The app couldn't open your browser."), _L("About JusPrin"), wxOK | wxICON_WARNING).ShowModal();
}

} // namespace

wxString AboutWindow::version_text()
{
    return wxString::FromUTF8(Updates::version_label() + ", build " + build_commit_label);
}

AboutWindow::AboutWindow(wxWindow* parent, const ShellTheme& theme)
    : wxDialog(parent, wxID_ANY, _L("About JusPrin"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_theme(theme)
    , m_copied(this)
{
    SetName("JusPrin About");
    const ShellPalette& palette = theme.palette(GUI_App::dark_mode());
    SetBackgroundColour(palette.surface_raised);

    // One panel holds the sections, so the window's width is the board's
    // whatever the platform puts around a dialog's client area.
    auto* body = new wxPanel(this);
    body->SetBackgroundColour(palette.surface_raised);
    auto* sections = new wxBoxSizer(wxVERTICAL);
    sections->Add(build_version(body), 0, wxEXPAND);
    sections->Add(rule(body), 0, wxEXPAND);
    sections->Add(build_help(body), 0, wxEXPAND);
    sections->Add(rule(body), 0, wxEXPAND);
    sections->Add(build_license(body), 0, wxEXPAND);
    sections->SetMinSize(FromDIP(wxSize(theme.metrics().about.width, -1)));
    body->SetSizer(sections);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(body, 1, wxEXPAND);
    SetSizerAndFit(sizer);

    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_ESCAPE)
            EndModal(wxID_CANCEL);
        else
            event.Skip();
    });
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { show_copy_label(false); }, m_copied.GetId());

    CentreOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

wxSizer* AboutWindow::build_version(wxWindow* parent)
{
    const ShellPalette& palette = m_theme.palette(GUI_App::dark_mode());
    const ShellMetrics& m       = m_theme.metrics();

    auto* logo = new AnimatedLockup(parent, m_theme, palette.surface_raised);
    logo->SetName("JusPrin");

    auto* version = new wxStaticText(parent, wxID_ANY, wxString::Format(_L("Version %s"), Updates::kVersion));
    style_label(*version, m_theme, TextRole::Section, palette.text_primary);
    auto* base = new wxStaticText(parent, wxID_ANY, wxString::Format(_L("Built on OrcaSlicer %s"), SoftFever_VERSION));
    style_label(*base, m_theme, TextRole::BodySmall, palette.text_secondary);

    // "Build", then the commit in the monospace face.
    auto* build_label = new wxStaticText(parent, wxID_ANY, _L("Build"));
    style_label(*build_label, m_theme, TextRole::Label, palette.text_secondary);
    auto* build_hash = new wxStaticText(parent, wxID_ANY, wxString::FromUTF8(build_commit_label));
    build_hash->SetFont(m_theme.code_font());
    build_hash->SetForegroundColour(palette.text_secondary);
    auto* build = new wxBoxSizer(wxHORIZONTAL);
    build->Add(build_label, 0, wxALIGN_CENTER_VERTICAL);
    build->Add(build_hash, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(m.space_1));

    m_copy = new Button(parent, _L("Copy version"));
    style_button(*m_copy, m_theme, palette, m.button.secondary_small);
    m_copy->SetIconSpacing(FromDIP(kButtonIconGapDip));
    show_copy_label(false);
    m_copy->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { copy_version(); });

    auto* lines = new wxBoxSizer(wxVERTICAL);
    lines->Add(version, 0, wxALIGN_CENTER_HORIZONTAL);
    lines->Add(base, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(m.space_1));
    lines->Add(build, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(m.space_1));

    auto* column = new wxBoxSizer(wxVERTICAL);
    column->Add(logo, 0, wxALIGN_CENTER_HORIZONTAL);
    column->Add(lines, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(m.space_4));
    column->Add(m_copy, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(m.space_4));

    auto* padded = new wxBoxSizer(wxVERTICAL);
    padded->AddSpacer(FromDIP(m.space_8));
    padded->Add(column, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(m.space_6));
    padded->AddSpacer(FromDIP(m.space_6));
    return padded;
}

wxSizer* AboutWindow::build_help(wxWindow* parent)
{
    const ShellPalette& palette = m_theme.palette(GUI_App::dark_mode());
    const ShellMetrics& m       = m_theme.metrics();

    auto* column = new wxBoxSizer(wxVERTICAL);
    auto* title  = new wxStaticText(parent, wxID_ANY, _L("Get help"));
    style_label(*title, m_theme, TextRole::LabelBold, palette.text_primary);
    column->Add(title, 0, wxBOTTOM, FromDIP(m.space_2));

    struct Row { wxString name; wxString shown; wxString url; };
    const Row rows[] = {
        {_L("Email"), kSupportEmail, wxString("mailto:") + kSupportEmail},
        {"Discord", kDiscordInvite, wxString("https://") + kDiscordInvite},
        {"GitHub", kRepository, wxString("https://") + kRepository},
    };
    for (const Row& row : rows) {
        auto* name = new wxStaticText(parent, wxID_ANY, row.name);
        style_label(*name, m_theme, TextRole::Label, palette.text_secondary);
        name->SetMinSize(wxSize(FromDIP(m.about.help_label_width), -1));
        auto* link = new Button(parent, row.shown);
        style_link_button(*link, m_theme, palette, m.button.link);
        link->Bind(wxEVT_BUTTON, [this, url = row.url](wxCommandEvent&) { open_link(this, url); });

        auto* line = new wxBoxSizer(wxHORIZONTAL);
        line->Add(name, 0, wxALIGN_CENTER_VERTICAL);
        line->Add(link, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(m.space_2));
        column->Add(line, 0, wxTOP, row.name == rows[0].name ? 0 : FromDIP(m.space_1));
    }

    auto* padded = new wxBoxSizer(wxVERTICAL);
    padded->AddSpacer(FromDIP(m.space_4));
    padded->Add(column, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(m.space_6));
    padded->AddSpacer(FromDIP(m.space_4));
    return padded;
}

wxSizer* AboutWindow::build_license(wxWindow* parent)
{
    const ShellPalette& palette = m_theme.palette(GUI_App::dark_mode());
    const ShellMetrics& m       = m_theme.metrics();

    auto* license = new Button(parent, _L("License…"));
    style_button(*license, m_theme, palette, m.button.secondary);
    license->Bind(wxEVT_BUTTON, [this, license](wxCommandEvent&) {
        CopyrightsDialog dialog;
        dialog.ShowModal();
        // The licence window's parent is always the main window, which takes
        // the focus back when it closes; give it to this window instead.
        Raise();
        license->SetFocus();
    });

    auto* note = new wxStaticText(parent, wxID_ANY, _L("Free software under the GNU Affero General Public License v3."));
    style_label(*note, m_theme, TextRole::Label, palette.text_secondary);
    // What the row leaves beside the button.
    note->Wrap(FromDIP(m.about.width - 2 * m.space_6 - m.space_3) - license->GetMinSize().GetWidth());

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(note, 1, wxALIGN_CENTER_VERTICAL);
    row->Add(license, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(m.space_3));

    auto* padded = new wxBoxSizer(wxVERTICAL);
    padded->AddSpacer(FromDIP(m.space_4));
    padded->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(m.space_6));
    padded->AddSpacer(FromDIP(m.space_5));
    return padded;
}

wxWindow* AboutWindow::rule(wxWindow* parent)
{
    auto* line = new Rule(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(1)));
    line->SetBackgroundColour(m_theme.palette(GUI_App::dark_mode()).border_subtle);
    return line;
}

void AboutWindow::copy_version()
{
    if (!wxTheClipboard->Open())
        return;
    wxTheClipboard->SetData(new wxTextDataObject(version_text()));
    wxTheClipboard->Close();
    show_copy_label(true);
    m_copied.StartOnce(m_theme.metrics().about.copied_ms);
}

void AboutWindow::show_copy_label(bool copied)
{
    const ShellPalette& palette = m_theme.palette(GUI_App::dark_mode());
    // The button's icon is a bitmap Button draws at its scaled size, so it is
    // rasterized at the screen's pixels and told their scale.
    const double scale  = GetContentScaleFactor();
    const int    pixels = std::max(1, int(std::lround(FromDIP(kButtonIconDip) * scale)));
    wxBitmap     icon   = lucide_bitmap(copied ? "check" : "copy", pixels, palette.text_primary);
    icon.SetScaleFactor(scale);
    m_copy->SetLabel(copied ? _L("Copied") : _L("Copy version"));
    m_copy->SetIcon(icon);
    m_copy->GetParent()->Layout();
}

} // namespace Slic3r::GUI::JusPrin
