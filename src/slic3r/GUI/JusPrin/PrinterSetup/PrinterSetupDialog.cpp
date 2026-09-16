#include "PrinterSetupDialog.hpp"

#include "PrinterPhoto.hpp"
#include "PrinterSetupLauncher.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentWebView.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellRecipes.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"

#include <wx/filedlg.h>
#include <wx/dcbuffer.h>
#include <wx/filename.h>
#include <wx/hyperlink.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/wupdlock.h>

#include <fstream>
#include <algorithm>
#include <cctype>
#include <functional>
#include <iterator>
#include <utility>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

namespace {

class RoundedPanel : public wxPanel
{
public:
    RoundedPanel(wxWindow* parent, const wxColour& fill, int radius, const wxSize& size,
                 const wxColour& border = wxTransparentColour, wxPenStyle border_style = wxPENSTYLE_SOLID)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, size, wxBORDER_NONE),
          m_fill(fill), m_border(border), m_radius(radius), m_border_style(border_style)
    {
        SetMinSize(size);
        // Children inherit this, so a label on the panel sits on its fill
        // rather than on the dialog's surface.
        SetBackgroundColour(fill);
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxAutoBufferedPaintDC dc(this);
            dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
            dc.Clear();
            dc.SetPen(m_border.IsOk() && m_border.Alpha() != 0
                          ? wxPen(m_border, FromDIP(1), m_border_style)
                          : *wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(m_fill));
            wxRect bounds = GetClientRect();
            bounds.Deflate(FromDIP(1));
            dc.DrawRoundedRectangle(bounds, FromDIP(m_radius));
        });
    }
private:
    wxColour m_fill;
    wxColour m_border;
    int m_radius;
    wxPenStyle m_border_style;
};

class FocusablePanel final : public RoundedPanel
{
public:
    using RoundedPanel::RoundedPanel;
    bool AcceptsFocus() const override { return true; }
};

class PhotoDropTarget final : public wxFileDropTarget
{
public:
    explicit PhotoDropTarget(std::function<bool(const wxString&)> accept) : m_accept(std::move(accept)) {}
    bool OnDropFiles(wxCoord, wxCoord, const wxArrayString& filenames) override
    {
        return filenames.size() == 1 && m_accept(filenames.front());
    }
private:
    std::function<bool(const wxString&)> m_accept;
};

wxString display_material(std::string value)
{
    if (const std::size_t at = value.find(" @"); at != std::string::npos)
        value.resize(at);
    return wxString::FromUTF8(value);
}

bool is_lan(const DiscoveredPrinter& printer)
{
    std::string connection = printer.connection;
    std::transform(connection.begin(), connection.end(), connection.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return connection == "lan";
}

} // namespace

PrinterSetupDialog::PrinterSetupDialog(wxWindow* parent, const ShellTheme& theme, bool dark,
                                       std::unique_ptr<PrinterSetupController> controller,
                                       std::vector<DiscoveredPrinter> discovered,
                                       bool agent_connected,
                                       MakeSetupWebViewFn make_setup_webview,
                                       ConnectFn connect,
                                       std::function<void()> on_agent_configured)
    : DPIDialog(parent, wxID_ANY, _L("Add a printer"), wxDefaultPosition, wxDefaultSize,
                wxBORDER_NONE),
      m_theme(theme), m_palette(theme.palette(dark)), m_controller(std::move(controller)),
      m_discovered(std::move(discovered)), m_agent_connected(agent_connected),
      m_make_setup_webview(std::move(make_setup_webview)), m_connect(std::move(connect)), m_timer(this),
      m_on_agent_configured(std::move(on_agent_configured))
{
    SetBackgroundColour(m_palette.surface_raised);
    SetName(_L("Add a printer"));
    m_root = new wxBoxSizer(wxVERTICAL);
    SetSizer(m_root);
    Bind(wxEVT_TIMER, &PrinterSetupDialog::on_timer, this);
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { EndModal(wxID_CANCEL); });
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_ESCAPE)
            EndModal(wxID_CANCEL);
        else
            event.Skip();
    });
    rebuild();
}

PrinterSetupDialog::~PrinterSetupDialog()
{
    m_timer.Stop();
    m_setup_pump_timer.Stop();
}

void PrinterSetupDialog::on_dpi_changed(const wxRect&) { rebuild(); }

wxStaticText* PrinterSetupDialog::label(wxWindow* parent, const wxString& text, TextRole role, const wxColour& color)
{
    auto* result = new wxStaticText(parent, wxID_ANY, text);
    style_label(*result, m_theme, role, color);
    result->Wrap(FromDIP(m_theme.metrics().printer_setup.dialog_width - 2 * m_theme.metrics().space_8));
    return result;
}

Button* PrinterSetupDialog::button(wxWindow* parent, const wxString& text, bool primary,
                                   std::function<void()> invoke)
{
    auto* result = new Button(parent, text);
    result->SetName(text);
    result->SetToolTip(text);
    if (primary) style_primary_button(*result, m_theme, m_palette, m_theme.metrics().button.primary);
    else style_button(*result, m_theme, m_palette, m_theme.metrics().button.secondary);
    result->Bind(wxEVT_BUTTON, [invoke = std::move(invoke)](wxCommandEvent&) { invoke(); });
    return result;
}

wxHyperlinkCtrl* PrinterSetupDialog::link(wxWindow* parent, const wxString& text,
                                          std::function<void()> invoke, bool accent)
{
    auto* result = new wxHyperlinkCtrl(parent, wxID_ANY, text, "action");
    result->SetName(text);
    wxFont font = m_theme.font(TextRole::BodySmallBold);
    font.SetUnderlined(accent);
    result->SetFont(font);
    const wxColour& colour = accent ? m_palette.action_primary : m_palette.text_primary;
    result->SetNormalColour(colour);
    result->SetVisitedColour(colour);
    result->SetHoverColour(m_palette.action_primary_hover);
    result->Bind(wxEVT_HYPERLINK, [invoke = std::move(invoke)](wxHyperlinkEvent&) { invoke(); });
    return result;
}

std::string PrinterSetupDialog::description_value() const
{
    return m_description == nullptr || m_description_is_prompt
        ? std::string()
        : std::string(m_description->GetValue().ToUTF8());
}

void PrinterSetupDialog::build_header(wxBoxSizer& content)
{
    const auto& metrics = m_theme.metrics();
    auto* header = new wxBoxSizer(wxHORIZONTAL);
    header->Add(label(this, _L("Add a printer"), TextRole::Section, m_palette.text_primary),
                1, wxALIGN_CENTER_VERTICAL);
    auto* close = button(this, _L("×"), false, [this] { EndModal(wxID_CANCEL); });
    style_button(*close, m_theme, m_palette, metrics.button.icon);
    close->SetName(_L("Close Add a printer"));
    header->Add(close, 0, wxALIGN_CENTER_VERTICAL);
    content.Add(header, 0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_5));
}

void PrinterSetupDialog::rebuild()
{
    wxWindowUpdateLocker lock(this);
    m_description = nullptr;
    m_description_is_prompt = false;
    // m_setup_webview outlives any single rebuild() -- including one from a
    // DPI change while its setup flow is still showing -- so detach it from
    // whatever content sizer currently holds it before that sizer's children
    // are destroyed below.
    if (m_setup_webview) {
        if (wxSizer* current = m_setup_webview->GetContainingSizer())
            current->Detach(m_setup_webview.get());
        m_setup_webview->Hide();
    }
    m_root->Clear(true);
    const auto& metrics = m_theme.metrics();
    auto* content = new wxBoxSizer(wxVERTICAL);
    build_header(*content);
    // States whose content varies (a network printer's details, a list of
    // candidates, the no-agent card) take their fitted height.
    int target_height = metrics.printer_setup.initial_height;
    if (m_showing_agent_setup) {
        // A wxWebView has no meaningful "fitted" content size the way a
        // native control does, so this keeps the same fixed height the
        // "no agent" card it replaces used; the page's own CSS already
        // scrolls internally when its content runs taller (see Setup.tsx).
        content->Add(m_setup_webview.get(), 1, wxEXPAND);
        m_setup_webview->Show();
    } else {
        switch (m_controller->state()) {
        case FlowState::Initial:
            if (m_agent_connected) {
                build_initial(*content);
            } else {
                target_height = 0;
                build_no_agent(*content);
            }
            break;
        case FlowState::Recognizing: build_recognizing(*content); break;
        case FlowState::Recognized:
            target_height = m_controller->evidence().discovered_device ? 0 : metrics.printer_setup.recognized_height;
            build_recognized(*content);
            break;
        case FlowState::Ambiguous:
            target_height = 0;
            build_ambiguous(*content);
            break;
        case FlowState::Error: build_error(*content); break;
        case FlowState::Complete: EndModal(wxID_OK); return;
        }
    }
    m_root->Add(content, 1, wxEXPAND | wxALL, FromDIP(metrics.space_6));
    Layout();
    Fit();
    const int fitted_height = GetClientSize().y;
    SetClientSize(wxSize(FromDIP(metrics.printer_setup.dialog_width),
                         std::max(FromDIP(target_height), fitted_height)));
    CentreOnParent();
}

void PrinterSetupDialog::rebuild_later()
{
    CallAfter([this] { rebuild(); });
}

void PrinterSetupDialog::build_network_section(wxBoxSizer& content)
{
    const auto& metrics = m_theme.metrics();
    content.Add(label(this, _L("FOUND ON YOUR NETWORK"), TextRole::MetadataBold, m_palette.text_secondary),
                0, wxBOTTOM, FromDIP(metrics.space_2));
    if (m_discovered.empty()) {
        content.Add(label(this, _L("No supported printers found yet."), TextRole::BodySmall, m_palette.text_secondary),
                    0, wxBOTTOM, FromDIP(metrics.space_4));
        return;
    }
    for (const DiscoveredPrinter& printer : m_discovered) {
        auto* row_panel = new RoundedPanel(this, m_palette.surface_canvas,
                                           metrics.printer_setup.control_radius,
                                           wxSize(-1, FromDIP(metrics.printer_setup.network_row_height)),
                                           m_palette.border_subtle);
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* dot = new RoundedPanel(row_panel,
                                     printer.connected ? m_palette.status_success : m_palette.action_disabled_text,
                                     metrics.radius_pill,
                                     row_panel->FromDIP(wxSize(metrics.space_2, metrics.space_2)));
        row->Add(dot, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, row_panel->FromDIP(metrics.space_2));
        row->Add(label(row_panel, wxString::FromUTF8(printer.name), TextRole::BodySmallBold,
                       m_palette.text_primary), 0, wxALIGN_CENTER_VERTICAL);
        row->Add(label(row_panel, wxString::Format(_L(" · %s"), wxString::FromUTF8(printer.stable_id)),
                       TextRole::Metadata, m_palette.text_secondary), 1, wxALIGN_CENTER_VERTICAL);
        row->Add(button(row_panel, _L("Use this"), false, [this, printer] {
            m_controller->use_discovered(printer);
            rebuild_later();
        }), 0, wxALIGN_CENTER_VERTICAL);
        row_panel->SetToolTip(printer.connected ? _L("Found on your network") : _L("Offline"));
        row_panel->SetSizer(row);
        row->AddSpacer(row_panel->FromDIP(metrics.space_3));
        row->Insert(0, row_panel->FromDIP(metrics.space_3), 0);
        content.Add(row_panel, 0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_3));
    }
}

void PrinterSetupDialog::build_initial(wxBoxSizer& content)
{
    const auto& metrics = m_theme.metrics();
    content.Add(label(this, _L("What printer do you have?"), TextRole::BodyBold, m_palette.text_primary),
                0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_2));
    auto* evidence_field = new RoundedPanel(this, m_palette.surface_canvas,
                                            metrics.printer_setup.control_radius,
                                            wxSize(-1, FromDIP(metrics.printer_setup.evidence_field_height)),
                                            m_palette.border_strong);
    auto* evidence_sizer = new wxBoxSizer(wxVERTICAL);
    m_description = new wxTextCtrl(evidence_field, wxID_ANY, {}, wxDefaultPosition,
                                   wxDefaultSize, wxTE_MULTILINE | wxBORDER_NONE);
    m_description->SetName(_L("What printer do you have?"));
    style_text_field(*m_description, m_theme, m_palette);
    const wxString prompt = _L("Say it any way: “bambu a1 mini”, “the ender with the touchscreen”, “not sure, it’s the small one”");
    if (m_draft.description.empty()) {
        m_description_is_prompt = true;
        m_description->ChangeValue(prompt);
        m_description->SetForegroundColour(m_palette.text_secondary);
    } else {
        m_description->ChangeValue(wxString::FromUTF8(m_draft.description));
    }
    evidence_sizer->Add(m_description, 1, wxEXPAND | wxALL, evidence_field->FromDIP(metrics.space_2));
    evidence_field->SetSizer(evidence_sizer);
    content.Add(evidence_field, 0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_3));

    auto* photo = new FocusablePanel(this, m_palette.surface_canvas, metrics.printer_setup.control_radius,
                                     wxSize(-1, FromDIP(metrics.printer_setup.photo_drop_height)),
                                     m_palette.border_subtle, wxPENSTYLE_SHORT_DASH);
    photo->SetName(_L("Choose a printer photo"));
    auto* photo_sizer = new wxBoxSizer(wxVERTICAL);
    const wxString photo_text = m_draft.image_name.empty()
        ? _L("or drop a photo of the printer, its nameplate, or the box")
        : wxString::Format(_L("Photo: %s"), wxString::FromUTF8(m_draft.image_name));
    photo_sizer->AddStretchSpacer();
    photo_sizer->Add(label(photo, photo_text, TextRole::BodySmall, m_palette.text_secondary), 0, wxALIGN_CENTER);
    photo_sizer->AddStretchSpacer();
    photo->SetSizer(photo_sizer);
    photo->SetDropTarget(new PhotoDropTarget([this](const wxString& path) {
        // The rebuild below recreates the field from the draft, so capture
        // what was typed first, as choose_photo does.
        m_draft.description = description_value();
        const bool accepted = load_photo(path);
        rebuild_later();
        return accepted;
    }));
    photo->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { choose_photo(); });
    photo->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_SPACE)
            choose_photo();
        else
            event.Skip();
    });
    content.Add(photo, 0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_4));
    if (!m_photo_error.empty())
        content.Add(label(this, m_photo_error, TextRole::BodySmall, m_palette.status_danger),
                    0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_3));

    build_network_section(content);

    content.AddStretchSpacer();
    auto* footer = new wxBoxSizer(wxHORIZONTAL);
    footer->Add(label(this, _L("Prefer the lists? "), TextRole::BodySmall, m_palette.text_secondary),
                0, wxALIGN_CENTER_VERTICAL);
    footer->Add(link(this, _L("Set it up myself"), [this] { open_manual_setup(); }, true),
                0, wxALIGN_CENTER_VERTICAL);
    footer->AddStretchSpacer();
    auto* next = button(this, _L("Next"), true, [this] { submit(); });
    next->Enable(!m_draft.description.empty() || !m_draft.image_bytes.empty());
    m_description->Bind(wxEVT_TEXT, [this, next](wxCommandEvent&) {
        next->Enable(!description_value().empty() || !m_draft.image_bytes.empty());
    });
    m_description->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) {
        if (m_description_is_prompt) {
            m_description_is_prompt = false;
            m_description->ChangeValue({});
            m_description->SetForegroundColour(m_palette.text_primary);
        }
        event.Skip();
    });
    m_description->Bind(wxEVT_KILL_FOCUS, [this, prompt](wxFocusEvent& event) {
        if (m_description && m_description->GetValue().empty()) {
            m_description_is_prompt = true;
            m_description->ChangeValue(prompt);
            m_description->SetForegroundColour(m_palette.text_secondary);
        }
        event.Skip();
    });
    footer->Add(next, 0, wxALIGN_CENTER_VERTICAL);
    content.Add(footer, 0, wxEXPAND);
}

// 18f. "Set up the agent" opens the same setup flow as the docked Agent
// panel, but hosted in a second, throwaway AgentWebView embedded in this
// dialog (see set_up_agent()) rather than redirecting to the docked one.
void PrinterSetupDialog::build_no_agent(wxBoxSizer& content)
{
    const auto& metrics = m_theme.metrics();
    auto* card = new RoundedPanel(this, m_palette.surface_subtle, metrics.printer_setup.radius,
                                  wxDefaultSize, m_palette.border_subtle);
    const int text_width = FromDIP(metrics.printer_setup.dialog_width - 2 * metrics.space_6 - 2 * metrics.space_4);
    auto* column = new wxBoxSizer(wxVERTICAL);
    auto* heading = new wxBoxSizer(wxHORIZONTAL);
    heading->Add(label(card, wxString::FromUTF8("✦"), TextRole::BodyBold, m_palette.action_primary),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, card->FromDIP(metrics.space_2));
    heading->Add(label(card, _L("No agent connected"), TextRole::BodyBold, m_palette.text_primary),
                 0, wxALIGN_CENTER_VERTICAL);
    column->Add(heading, 0, wxBOTTOM, card->FromDIP(metrics.space_2));
    auto* pitch = label(card, _L("Tell the agent “the small bambu one”, or drop a photo, and it finds the right printer for you. No lists to search."),
                        TextRole::BodySmall, m_palette.text_secondary);
    pitch->Wrap(text_width);
    column->Add(pitch, 0, wxEXPAND | wxBOTTOM, card->FromDIP(metrics.space_3));
    column->Add(button(card, _L("Set up the agent"), true, [this] { set_up_agent(); }),
                0, wxALIGN_LEFT | wxBOTTOM, card->FromDIP(metrics.space_2));
    auto* footnote = label(card, _L("About a minute. Your own key, or an AI tool you already use."),
                           TextRole::BodySmall, m_palette.text_secondary);
    footnote->Wrap(text_width);
    column->Add(footnote, 0, wxEXPAND);
    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(column, 1, wxEXPAND | wxALL, card->FromDIP(metrics.space_4));
    card->SetSizer(outer);
    content.Add(card, 0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_5));

    build_network_section(content);

    auto* footer = new wxBoxSizer(wxHORIZONTAL);
    footer->Add(label(this, _L("Or "), TextRole::BodySmall, m_palette.text_secondary), 0, wxALIGN_CENTER_VERTICAL);
    footer->Add(link(this, _L("set it up myself"), [this] { open_manual_setup(); }, true), 0, wxALIGN_CENTER_VERTICAL);
    footer->Add(label(this, _L(" from the lists"), TextRole::BodySmall, m_palette.text_secondary), 0, wxALIGN_CENTER_VERTICAL);
    content.Add(footer, 0, wxEXPAND | wxTOP, FromDIP(metrics.space_1));
}

void PrinterSetupDialog::build_recognizing(wxBoxSizer& content)
{
    content.Add(label(this, _L("Looking for a matching printer profile…"), TextRole::Body,
                      m_palette.text_secondary), 0, wxEXPAND | wxBOTTOM, FromDIP(m_theme.metrics().space_6));
    content.Add(button(this, _L("Cancel"), false, [this] {
        m_controller->start_over();
        m_timer.Stop();
        rebuild_later();
    }), 0, wxALIGN_RIGHT);
}

wxPanel* PrinterSetupDialog::candidate_card(wxWindow* parent, const PrinterCandidate& candidate, bool compact,
                                            const wxString& action, std::function<void()> invoke)
{
    const auto& metrics = m_theme.metrics();
    const int card_height = compact
        ? metrics.printer_setup.ambiguous_artwork_height + metrics.space_12 + metrics.space_10
        : metrics.printer_setup.candidate_min_height;
    auto* card = new RoundedPanel(parent,
                                  compact ? m_palette.surface_canvas : m_palette.surface_subtle,
                                  metrics.printer_setup.radius,
                                  wxSize(-1, parent->FromDIP(card_height)),
                                  compact ? m_palette.border_subtle : wxTransparentColour);

    auto artwork = [&](wxWindow* artwork_parent, int size) -> wxWindow* {
        auto* holder = new RoundedPanel(artwork_parent, m_palette.surface_selected,
                                        metrics.printer_setup.control_radius,
                                        artwork_parent->FromDIP(wxSize(size, size)));
        if (!candidate.artwork_path.empty()) {
            wxImage image(wxString::FromUTF8(candidate.artwork_path));
            if (image.IsOk()) {
                const int pixels = holder->FromDIP(size);
                image.Rescale(pixels, pixels, wxIMAGE_QUALITY_HIGH);
                auto* holder_sizer = new wxBoxSizer(wxVERTICAL);
                holder_sizer->Add(new wxStaticBitmap(holder, wxID_ANY, wxBitmap(image)), 1,
                                  wxALIGN_CENTER | wxALL, holder->FromDIP(metrics.space_1));
                holder->SetSizer(holder_sizer);
            }
        }
        return holder;
    };

    if (compact) {
        auto* column = new wxBoxSizer(wxVERTICAL);
        auto* art = artwork(card, metrics.printer_setup.ambiguous_artwork_height);
        art->SetMinSize(wxSize(-1, card->FromDIP(metrics.printer_setup.ambiguous_artwork_height)));
        column->Add(art, 0, wxEXPAND | wxBOTTOM, card->FromDIP(metrics.space_3));
        column->Add(label(card, wxString::FromUTF8(candidate.model_name), TextRole::BodyBold,
                          m_palette.text_primary), 0, wxBOTTOM, card->FromDIP(metrics.space_2));
        if (!action.empty())
            column->Add(button(card, action, false, std::move(invoke)), 0, wxALIGN_LEFT);
        auto* outer = new wxBoxSizer(wxVERTICAL);
        outer->Add(column, 1, wxEXPAND | wxALL, card->FromDIP(metrics.space_3));
        card->SetSizer(outer);
        return card;
    }

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(artwork(card, metrics.printer_setup.artwork_size), 0,
             wxALIGN_CENTER_VERTICAL | wxRIGHT, card->FromDIP(metrics.space_4));
    auto* details = new wxBoxSizer(wxVERTICAL);
    details->AddStretchSpacer();
    details->Add(label(card, wxString::FromUTF8(candidate.model_name), TextRole::Section,
                       m_palette.text_primary), 0, wxBOTTOM, card->FromDIP(metrics.space_1));
    if (const auto& device = m_controller->evidence().discovered_device; device) {
        // 18c: what the printer said about itself. The plate is the profile's,
        // since the device does not report one.
        const wxString nozzle = device->nozzle_diameter > 0.
            ? wxString::Format(_L("%.1f mm nozzle"), device->nozzle_diameter)
            : wxString::Format(_L("%s mm nozzle"), wxString::FromUTF8(candidate.variant));
        const wxString hardware = candidate.default_plate.empty()
            ? nozzle : nozzle + wxString::FromUTF8(" · ") + wxString::FromUTF8(candidate.default_plate);
        details->Add(label(card, hardware, TextRole::BodySmall, m_palette.text_secondary));
        if (!device->spools.empty()) {
            auto* spools = new wxBoxSizer(wxHORIZONTAL);
            const wxString unit = device->ams_name.empty() ? _L("AMS") : wxString::FromUTF8(device->ams_name);
            spools->Add(label(card, unit + ": ", TextRole::BodySmall, m_palette.text_secondary), 0, wxALIGN_CENTER_VERTICAL);
            spools->Add(label(card, wxString::FromUTF8("● "), TextRole::BodySmall,
                              wxColour(wxString::FromUTF8(device->spools.front().colour))), 0, wxALIGN_CENTER_VERTICAL);
            wxString names = wxString::FromUTF8(device->spools.front().name);
            if (device->spools.size() > 1)
                names += wxString::Format(_L(" + %zu more"), device->spools.size() - 1);
            spools->Add(label(card, names, TextRole::BodySmall, m_palette.text_secondary), 0, wxALIGN_CENTER_VERTICAL);
            details->Add(spools, 0, wxTOP, card->FromDIP(metrics.space_1));
        }
        if (device->nozzle_diameter > 0. || !device->spools.empty())
            details->Add(label(card, _L("read from the printer just now"), TextRole::Metadata, m_palette.text_secondary),
                         0, wxTOP, card->FromDIP(metrics.space_1));
    } else if (!candidate.build_volume.empty()) {
        details->Add(label(card, wxString::FromUTF8(candidate.build_volume), TextRole::BodySmall,
                           m_palette.text_secondary));
    }
    details->AddStretchSpacer();
    row->Add(details, 1, wxEXPAND);
    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(row, 1, wxEXPAND | wxALL, card->FromDIP(metrics.space_3));
    card->SetSizer(outer);
    return card;
}

void PrinterSetupDialog::build_recognized(wxBoxSizer& content)
{
    const auto& metrics = m_theme.metrics();
    const PrinterCandidate& candidate = *m_controller->candidates().front();
    const auto& device = m_controller->evidence().discovered_device;
    if (device) {
        content.Add(label(this, wxString::Format(_L("●  Found on your network · %s"), wxString::FromUTF8(device->stable_id)),
                          TextRole::BodySmallBold, m_palette.status_success),
                    0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_4));
    } else if (!m_controller->evidence_summary().empty()) {
        content.Add(label(this, wxString::Format(wxString::FromUTF8("“%s”"), wxString::FromUTF8(m_controller->evidence_summary())),
                          TextRole::Body, m_palette.text_secondary), 0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_4));
    }
    content.Add(candidate_card(this, candidate, false, {}, {}), 0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_4));

    const bool reported = device && (device->nozzle_diameter > 0. || !device->spools.empty());
    const bool lan = device && is_lan(*device);
    wxString assumption;
    if (reported) {
        assumption = _L("Nothing to assume: the printer told me its nozzle and spools.");
    } else {
        const wxString material = candidate.default_material.empty() ? _L("PLA")
                                                                      : display_material(candidate.default_material);
        const wxString plate = candidate.default_plate.empty() ? _L("default build plate")
                                                                : wxString::FromUTF8(candidate.default_plate);
        assumption = wxString::Format(_L("I’ll assume a %s mm nozzle, the %s it ships with, and %s. If any of that is different, tell me here or in your first project."),
                                      wxString::FromUTF8(candidate.variant), plate, material);
    }
    if (lan)
        assumption += " " + _L("To keep it connected, enter its access code (on the printer: Settings › Network) or sign in to Bambu.");
    content.Add(label(this, assumption, TextRole::BodySmall, m_palette.text_primary),
                0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_4));
    // A correction the matched profile cannot represent stays visible as not
    // applied; it is never shown as accepted and then dropped.
    if (!m_controller->unresolved_correction().empty()) {
        auto* unresolved = label(this, wxString::Format(_L("Not applied: “%s”. This printer profile has no setting for it."),
                                                        wxString::FromUTF8(m_controller->unresolved_correction())),
                                 TextRole::BodySmall, m_palette.status_danger);
        unresolved->SetName(_L("Correction not applied"));
        content.Add(unresolved, 0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_4));
    }

    // A network printer has nothing to correct; a LAN one takes its access
    // code instead. Everything else keeps the correction field.
    wxTextCtrl* field = nullptr;
    if (lan || !device) {
        auto* field_panel = new RoundedPanel(this, m_palette.surface_canvas,
                                             metrics.printer_setup.control_radius,
                                             wxSize(-1, FromDIP(metrics.printer_setup.correction_field_height)),
                                             m_palette.border_subtle);
        auto* field_sizer = new wxBoxSizer(wxVERTICAL);
        field = new wxTextCtrl(field_panel, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize,
                               wxTE_PROCESS_ENTER | wxBORDER_NONE);
        style_text_field(*field, m_theme, m_palette);
        if (lan) {
            field->SetName(_L("Access code"));
            field->SetHint(_L("access code, optional"));
            field->ChangeValue(wxString::FromUTF8(m_access_code));
        } else {
            field->SetName(_L("Correction"));
            field->SetHint(_L("e.g. “I put a 0.6 nozzle on it” · “it’s the Combo with the AMS”"));
        }
        field_sizer->Add(field, 1, wxEXPAND | wxALL, field_panel->FromDIP(metrics.space_2));
        field_panel->SetSizer(field_sizer);
        content.Add(field_panel, 0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_4));
    }
    if (lan && !m_access_error.empty())
        content.Add(label(this, m_access_error, TextRole::BodySmall, m_palette.status_danger),
                    0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_4));

    content.AddStretchSpacer();
    auto* actions = new wxBoxSizer(wxHORIZONTAL);
    actions->Add(link(this, _L("Not this one"), [this] {
        m_access_code.clear();
        m_access_error.clear();
        m_controller->start_over();
        rebuild_later();
    }), 0, wxALIGN_CENTER_VERTICAL);
    actions->AddStretchSpacer();
    auto* confirm = button(this, _L("Add this printer"), true, [this, field, lan] {
        const std::string value = field ? std::string(field->GetValue().ToUTF8()) : std::string();
        if (lan) {
            m_access_code = value;
            m_access_error.clear();
            if (!value.empty() && !m_connect(*m_controller->evidence().discovered_device, value, m_access_error)) {
                rebuild_later();
                return;
            }
        } else if (!value.empty()) {
            m_controller->correct(value);
            if (m_controller->state() == FlowState::Recognizing) m_timer.Start(50);
            rebuild_later();
            return;
        }
        if (m_controller->confirm())
            EndModal(wxID_OK);
        else
            rebuild_later();
    });
    if (field) {
        if (!lan)
            field->Bind(wxEVT_TEXT, [confirm, field](wxCommandEvent&) {
                confirm->SetLabel(field->GetValue().empty() ? _L("Add this printer") : _L("Check correction"));
            });
        field->Bind(wxEVT_TEXT_ENTER, [confirm](wxCommandEvent&) {
            wxCommandEvent click(wxEVT_BUTTON, confirm->GetId());
            wxPostEvent(confirm, click);
        });
    }
    actions->Add(confirm, 0, wxALIGN_CENTER_VERTICAL);
    content.Add(actions, 0, wxEXPAND);
}

void PrinterSetupDialog::build_ambiguous(wxBoxSizer& content)
{
    const auto& metrics = m_theme.metrics();
    if (!m_controller->evidence_summary().empty())
        content.Add(label(this, wxString::Format(wxString::FromUTF8("“%s”"), wxString::FromUTF8(m_controller->evidence_summary())),
                          TextRole::Body, m_palette.text_secondary), 0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_3));
    const wxString explanation = m_controller->uncertain()
        ? _L("This looks like the closest match, but I’m not sure. Confirm it’s yours, or upload a photo.")
        : m_controller->assumption().empty()
        ? _L("I found more than one close match. Choose the model that looks like yours.")
        : wxString::FromUTF8(m_controller->assumption());
    content.Add(label(this, explanation, TextRole::BodySmall, m_palette.text_primary),
                0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_4));
    auto* choices = new wxBoxSizer(wxHORIZONTAL);
    const auto& candidates = m_controller->candidates();
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const PrinterCandidate* candidate = candidates[index];
        choices->Add(candidate_card(this, *candidate, true, _L("This one"), [this, id = candidate->id] {
            m_controller->choose(id);
            rebuild_later();
        }), 1, wxEXPAND);
        if (index + 1 < candidates.size())
            choices->AddSpacer(FromDIP(metrics.space_3));
    }
    content.Add(choices, 0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_4));

    auto* photo_hint = new wxBoxSizer(wxHORIZONTAL);
    photo_hint->Add(label(this, _L("Not sure? "), TextRole::BodySmall, m_palette.text_secondary),
                    0, wxALIGN_CENTER_VERTICAL);
    photo_hint->Add(link(this, _L("A photo of the front"), [this] { choose_photo(true); }),
                    0, wxALIGN_CENTER_VERTICAL);
    photo_hint->Add(label(this, _L(" settles it."), TextRole::BodySmall, m_palette.text_secondary),
                    1, wxALIGN_CENTER_VERTICAL);
    content.Add(photo_hint, 0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_3));
    if (!m_photo_error.empty())
        content.Add(label(this, m_photo_error, TextRole::BodySmall, m_palette.status_danger),
                    0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_3));

    auto* more_panel = new RoundedPanel(this, m_palette.surface_canvas, metrics.printer_setup.control_radius,
                                        wxSize(-1, FromDIP(metrics.printer_setup.evidence_field_height)),
                                        m_palette.border_subtle);
    auto* more_sizer = new wxBoxSizer(wxVERTICAL);
    auto* more = new wxTextCtrl(more_panel, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize,
                                wxTE_PROCESS_ENTER | wxBORDER_NONE);
    more->SetName(_L("Say more"));
    style_text_field(*more, m_theme, m_palette);
    more->SetHint(_L("or say more: “it has a knob”, “the box says S1 Pro”"));
    more->Bind(wxEVT_TEXT_ENTER, [this, more](wxCommandEvent&) {
        m_controller->clarify(std::string(more->GetValue().ToUTF8()));
        if (m_controller->state() == FlowState::Recognizing) m_timer.Start(50);
        rebuild_later();
    });
    more_sizer->AddStretchSpacer();
    more_sizer->Add(more, 0, wxEXPAND | wxLEFT | wxRIGHT, more_panel->FromDIP(metrics.space_3));
    more_sizer->AddStretchSpacer();
    more_panel->SetSizer(more_sizer);
    content.Add(more_panel, 0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_5));

    content.AddStretchSpacer();
    content.Add(link(this, _L("Neither · it's not in the list"), [this] { start_over(); }), 0, wxALIGN_LEFT);
}

void PrinterSetupDialog::build_error(wxBoxSizer& content)
{
    const auto& metrics = m_theme.metrics();
    content.Add(label(this, wxString::FromUTF8(m_controller->error()), TextRole::Body, m_palette.status_danger),
                0, wxEXPAND | wxBOTTOM, FromDIP(metrics.space_4));
    auto* actions = new wxBoxSizer(wxHORIZONTAL);
    actions->Add(button(this, _L("Set it up manually"), false, [this] { open_manual_setup(); }), 1);
    actions->AddSpacer(FromDIP(metrics.space_2));
    if (m_controller->retryable())
        actions->Add(button(this, _L("Retry"), true, [this] {
            m_controller->retry();
            if (m_controller->state() == FlowState::Recognizing) m_timer.Start(50);
            rebuild_later();
        }), 0);
    else
        actions->Add(button(this, _L("Start over"), true, [this] { start_over(); }), 0);
    content.Add(actions, 0, wxEXPAND);
}

void PrinterSetupDialog::choose_photo(bool submit_after_load)
{
    // From 18a the draft is what the user is composing; from any later state
    // it is the evidence that produced that state, which the photo joins.
    if (m_description)
        m_draft.description = description_value();
    else
        m_draft = m_controller->evidence();
    wxFileDialog picker(this, _L("Choose a printer photo"), {}, {},
                        _L("Image files (*.png;*.jpg;*.jpeg;*.webp)|*.png;*.jpg;*.jpeg;*.webp"),
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (picker.ShowModal() == wxID_OK) {
        const bool accepted = load_photo(picker.GetPath());
        if (accepted && submit_after_load)
            submit();
        else
            rebuild_later();
    }
}

bool PrinterSetupDialog::load_photo(const wxString& path)
{
    std::ifstream input(std::string(path.ToUTF8()), std::ios::binary);
    if (!input) {
        m_photo_error = _L("That photo could not be read. Choose another file.");
        return false;
    }
    std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const PhotoCheck check = check_photo(bytes);
    if (check.mime.empty()) {
        m_photo_error = _(check.error);
        return false;
    }
    const std::string mime = check.mime;
    // The signature can be right while the body is truncated or corrupt.
    wxMemoryInputStream stream(bytes.data(), bytes.size());
    wxImage decoded(stream);
    if (!decoded.IsOk()) {
        m_photo_error = _L("That file is not a readable image. Choose another photo.");
        return false;
    }
    m_draft.image_bytes = std::move(bytes);
    m_draft.image_mime = mime;
    m_draft.image_name = std::string(wxFileName(path).GetFullName().ToUTF8());
    m_photo_error.clear();
    return true;
}

void PrinterSetupDialog::submit()
{
    if (m_description) m_draft.description = description_value();
    m_controller->recognize(m_draft);
    if (m_controller->state() == FlowState::Recognizing) m_timer.Start(50);
    rebuild_later();
}

void PrinterSetupDialog::start_over()
{
    m_timer.Stop();
    m_controller->start_over();
    m_draft = {};
    m_photo_error.clear();
    rebuild_later();
}

void PrinterSetupDialog::open_manual_setup()
{
    EndModal(wxID_CANCEL);
    wxGetApp().CallAfter([] {
        if (Plater* plater = wxGetApp().plater())
            run_manual_setup(*plater);
    });
}

void PrinterSetupDialog::set_up_agent()
{
    if (!m_setup_webview && m_make_setup_webview) {
        m_setup_webview = m_make_setup_webview(this);
        if (m_setup_webview) {
            m_setup_webview->Hide();
            // Runs once, when this throwaway host's own credential check
            // succeeds. Deferred with CallAfter because it fires from inside
            // AgentHost::pump_setup(), which is itself a member call on the
            // very host m_setup_webview owns -- resetting m_setup_webview
            // synchronously here would destroy that call's own object while
            // it is still on the stack.
            m_setup_webview->host().set_setup_completed_listener([this] {
                m_showing_agent_setup = false;
                m_agent_connected = true;
                if (m_on_agent_configured)
                    m_on_agent_configured();
                CallAfter([this] {
                    m_setup_pump_timer.Stop();
                    m_setup_webview.reset();
                    rebuild();
                });
            });
            m_setup_webview->host().request_setup();
            m_setup_pump_timer.Start(33);
        }
    }
    if (!m_setup_webview)
        return; // No factory wired (e.g. a degraded call site); nothing to show.
    m_showing_agent_setup = true;
    rebuild();
}

void PrinterSetupDialog::on_timer(wxTimerEvent& event)
{
    if (m_setup_webview && event.GetId() == m_setup_pump_timer.GetId()) {
        auto& host = m_setup_webview->host();
        host.pump_stream();
        host.pump_tools();
        host.pump_setup();
        return;
    }
    const FlowState before = m_controller->state();
    m_controller->poll();
    if (m_controller->state() != before) {
        m_timer.Stop();
        rebuild();
    }
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
