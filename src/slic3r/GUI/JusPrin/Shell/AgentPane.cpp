#include "AgentPane.hpp"
#include "McpConnectionDialog.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentWebView.hpp"
#include "slic3r/GUI/JusPrin/Mcp/McpRuntime.hpp"

#include <boost/system/system_error.hpp>
#include <wx/button.h>
#include <wx/sizer.h>

namespace Slic3r::GUI::JusPrin {

AgentPane::AgentPane(wxWindow*                  parent,
                     const ShellTheme&          theme,
                     Workspace::IWorkspace&     workspace,
                     Agent::ProjectPersistence& persistence,
                     Agent::AgentAvailability   availability,
                     Agent::AgentServicePtr      agent,
                     Agent::AgentSetupServicePtr setup,
                     const std::string& discovery_path)
    : wxPanel(parent, wxID_ANY)
    , m_theme(theme)
{
    SetMinSize(wxSize(FromDIP(320), -1));

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    m_web_view = new AgentWebView(this, theme, workspace, persistence, availability, std::move(agent),
                                  std::move(setup));
    sizer->Add(m_web_view, 1, wxEXPAND);
    std::string startup_error;
    try {
        m_web_view->host().start_mcp(discovery_path);
    } catch (const boost::system::system_error& error) {
        // Keep slicing and the in-app Agent usable if the listener fails;
        // the connection dialog exposes the startup error.
        startup_error = error.what();
    } catch (const std::filesystem::filesystem_error& error) {
        // Discovery failure disables MCP, not the slicer. Show the actionable
        // path/error in the same connection surface as a bind failure.
        startup_error = error.what();
    } catch (const std::ios_base::failure& error) {
        startup_error = "Could not write MCP discovery at " + discovery_path + ": " + error.what();
    }
    auto* diagnostics = new wxButton(this, wxID_ANY, "Connect AI tools...");
    diagnostics->Bind(wxEVT_BUTTON, [this, startup_error, discovery_path](wxCommandEvent&) {
        const auto* runtime = m_web_view->host().mcp();
        show_mcp_connection_dialog(this, m_theme, discovery_path, runtime ? runtime->server().url() : "", startup_error);
    });
    sizer->Add(diagnostics, 0, wxEXPAND | wxALL, FromDIP(8));
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
