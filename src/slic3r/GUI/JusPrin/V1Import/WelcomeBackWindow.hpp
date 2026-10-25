#pragma once

// The window shown once after JusPrin v1's setup was imported (V1Import.hpp):
// the Agent page in its welcome-back mode, drawn from the canvas boards
// V1-01 to V1-03 ("When JusPrin v1 is on this computer"). It lists what came
// along and shows the JusPrin account v1 was signed in to, checked against
// the server, with the account row's sign-in when that sign-in has ended.
// Modal, like Preferences, so nothing changes underneath it.

#include "V1Import.hpp"

#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"

#include <nlohmann/json_fwd.hpp>

#include <wx/dialog.h>
#include <wx/timer.h>

#include <memory>
#include <string>

namespace Slic3r::GUI::JusPrin {
class AgentWebView;
namespace Workspace { class IWorkspace; }
namespace Agent { class ProjectPersistence; }
}

namespace Slic3r::GUI::JusPrin::V1Import {

class WelcomeBackWindow : public wxDialog
{
public:
    WelcomeBackWindow(wxWindow* parent, const ShellTheme& theme, Workspace::IWorkspace& workspace, Report report);
    ~WelcomeBackWindow() override;

private:
    nlohmann::json state() const;
    bool           on_page_message(const std::string& type, const nlohmann::json& payload);
    void           on_pump(wxTimerEvent& event);

    const ShellTheme&                          m_theme;
    Report                                     m_report;
    // Imported presets OrcaSlicer did not load, by name: most often one based
    // on a printer this build has no profile for.
    std::vector<std::string>                   m_unusable;
    std::unique_ptr<Agent::ProjectPersistence> m_persistence;
    AgentWebView*                              m_view{nullptr};
    wxTimer                                    m_pump;
};

} // namespace Slic3r::GUI::JusPrin::V1Import
