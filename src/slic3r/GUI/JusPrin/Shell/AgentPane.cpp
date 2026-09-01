#include "AgentPane.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentWebView.hpp"

#include <wx/sizer.h>

namespace Slic3r::GUI::JusPrin {

AgentPane::AgentPane(wxWindow*                  parent,
                     const ShellTheme&          theme,
                     Workspace::IWorkspace&     workspace,
                     Agent::ProjectPersistence& persistence,
                     Agent::AgentAvailability   availability,
                     Agent::AgentServicePtr      agent)
    : wxPanel(parent, wxID_ANY)
    , m_theme(theme)
{
    SetMinSize(wxSize(FromDIP(320), -1));

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    m_web_view = new AgentWebView(this, theme, workspace, persistence, availability, std::move(agent));
    sizer->Add(m_web_view, 1, wxEXPAND);
    SetSizer(sizer);

    Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& event) {
        apply_appearance(GUI_App::dark_mode());
        event.Skip();
    });
}

void AgentPane::apply_appearance(bool dark)
{
    const ShellPalette& palette = m_theme.palette(dark);
    SetBackgroundColour(palette.surface_subtle);
    m_web_view->apply_appearance(dark);
    Refresh();
}

} // namespace Slic3r::GUI::JusPrin
