#include "AgentPane.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>

namespace Slic3r::GUI::JusPrin {

AgentPane::AgentPane(wxWindow* parent, const ShellTheme& theme)
    : wxPanel(parent, wxID_ANY)
    , m_theme(theme)
{
    SetMinSize(wxSize(FromDIP(300), -1));

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    m_title = new wxStaticText(this, wxID_ANY, _L("Agent"));
    m_title->SetFont(Label::Head_14);

    m_message = new wxStaticText(this, wxID_ANY, _L("The Agent is not available yet"));
    m_message->SetFont(Label::Body_14);

    m_detail = new wxStaticText(this, wxID_ANY,
                                _L("This build does not include the Agent service. Prepare, slice, and check "
                                   "your print with the controls above and the 3D canvas."));
    m_detail->SetFont(Label::Body_12);

    sizer->Add(m_title, 0, wxLEFT | wxTOP | wxRIGHT, FromDIP(16));
    sizer->AddSpacer(FromDIP(24));
    sizer->Add(m_message, 0, wxLEFT | wxRIGHT, FromDIP(16));
    sizer->AddSpacer(FromDIP(8));
    sizer->Add(m_detail, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(16));
    SetSizer(sizer);

    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        m_detail->Wrap(std::max(FromDIP(120), event.GetSize().GetWidth() - FromDIP(32)));
        event.Skip();
    });
    Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& event) {
        apply_appearance(GUI_App::dark_mode());
        event.Skip();
    });
}

void AgentPane::apply_appearance(bool dark)
{
    const ShellPalette& palette = m_theme.palette(dark);
    SetBackgroundColour(palette.surface_subtle);
    m_title->SetForegroundColour(palette.text_primary);
    m_message->SetForegroundColour(palette.text_primary);
    m_detail->SetForegroundColour(palette.text_secondary);
    Refresh();
}

} // namespace Slic3r::GUI::JusPrin
