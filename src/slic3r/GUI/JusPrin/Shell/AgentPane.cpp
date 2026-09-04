#include "AgentPane.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentWebView.hpp"
#include "slic3r/GUI/JusPrin/Mcp/McpCatalog.hpp"
#include "slic3r/GUI/JusPrin/Mcp/McpRuntime.hpp"

#include <boost/system/system_error.hpp>
#include <wx/sizer.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

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
        startup_error = error.what();
    } catch (const std::filesystem::filesystem_error& error) {
        startup_error = error.what();
    } catch (const std::ios_base::failure& error) {
        startup_error = "Could not write MCP discovery at " + discovery_path + ": " + error.what();
    }
#ifdef _WIN32
    const std::string helper_name = "jusprin-mcp.exe";
#else
    const std::string helper_name = "jusprin-mcp";
#endif
    const auto executable = std::filesystem::u8path(wxStandardPaths::Get().GetExecutablePath().ToUTF8().data());
    const auto helper = executable.parent_path() / helper_name;
    auto launcher = helper;
    std::vector<std::string> launch_arguments;
#if defined(__linux__)
    wxString appimage;
    if (wxGetEnv("APPIMAGE", &appimage) && !appimage.empty()) {
        launcher = std::filesystem::u8path(appimage.ToUTF8().data());
        launch_arguments.emplace_back("--mcp-bridge");
    }
#endif
    const auto paths = Mcp::default_catalog_paths();
    Agent::AgentHost::McpConnectSettings settings;
    settings.helper_path = helper.u8string();
    settings.launcher_path = launcher.u8string();
    settings.launch_arguments = std::move(launch_arguments);
    settings.startup_error = std::move(startup_error);
    settings.home = paths.home;
    settings.config_home = paths.config_home;
    settings.windows = paths.windows;
    m_web_view->host().configure_mcp_connect(std::move(settings));
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
