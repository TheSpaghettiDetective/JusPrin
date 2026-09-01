#pragma once

#include "ShellTheme.hpp"

#include "slic3r/GUI/JusPrin/Agent/AgentProtocol.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentService.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <wx/panel.h>

namespace Slic3r::GUI::JusPrin {

class AgentWebView;

// Fixed right-hand region holding the Agent conversation. The body is the
// packaged local React page in a wxWebView; every conversation, empty,
// unavailable, and bridge-error state is owned by AgentWebView and the page.
class AgentPane : public wxPanel
{
public:
    AgentPane(wxWindow*                  parent,
              const ShellTheme&          theme,
              Workspace::IWorkspace&     workspace,
              Agent::ProjectPersistence& persistence,
              Agent::AgentAvailability   availability,
              Agent::AgentServicePtr      agent = {});

    void apply_appearance(bool dark);

    AgentWebView& web_view() { return *m_web_view; }

private:
    const ShellTheme& m_theme;
    AgentWebView*     m_web_view{nullptr};
};

} // namespace Slic3r::GUI::JusPrin
