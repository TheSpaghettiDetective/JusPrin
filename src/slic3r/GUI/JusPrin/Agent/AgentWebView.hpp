#pragma once

// wx transport for the Agent bridge: hosts the packaged local React page in a
// wxWebView, forwards JSON envelopes between the page and the AgentHost, and
// presents the internal-connection error state (with Retry and diagnostics)
// when the page or the bridge cannot come up. The Agent-unavailable state, by
// contrast, is a page-rendered product state — the two failures are different.

#include "AgentHost.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"

#include <wx/panel.h>
#include <wx/timer.h>

#include <memory>

class Button;
class wxWebView;
class wxWebViewEvent;
class wxStaticText;
class wxBoxSizer;

namespace Slic3r::GUI::JusPrin {

// Which of the page's surfaces this view is. The page reads it from the query
// string it is loaded with (see main.tsx):
//   Conversation   the docked panel: header, chat list, thread, composer;
//   EmbeddedSetup  a throwaway setup-only instance, the setup screens alone;
//   PrinterPanel   Home's printer conversation: the printer session's own
//                  header, pinned card and chips around the same thread.
enum class AgentPageMode { Conversation, EmbeddedSetup, PrinterPanel };

class AgentWebView : public wxPanel
{
public:
    AgentWebView(wxWindow*                        parent,
                 const ShellTheme&                theme,
                 Workspace::IWorkspace&           workspace,
                 Agent::ProjectPersistence&       persistence,
                 Agent::AgentAvailability         availability,
                 Agent::AgentServicePtr            agent = {},
                 Agent::AgentSetupServicePtr       setup = {},
                 AgentPageMode                     mode = AgentPageMode::Conversation);
    ~AgentWebView() override;

    void apply_appearance(bool dark);
    void reload();

    Agent::AgentHost&    host() { return *m_host; }
    wxWebView*           webview() const { return m_webview; }
    bool                 bridge_error_shown() const { return m_error_panel != nullptr && m_error_panel->IsShown(); }

private:
    void on_script_message(wxWebViewEvent& event);
    void on_load_error(wxWebViewEvent& event);
    void on_handshake_deadline(wxTimerEvent& event);
    void show_bridge_error(const wxString& reason);
    void hide_bridge_error();
    wxString diagnostics_text(const wxString& reason) const;

    const ShellTheme&                 m_theme;
    std::unique_ptr<Agent::AgentHost> m_host;
    wxWebView*                        m_webview{nullptr};

    wxPanel*      m_error_panel{nullptr};
    wxStaticText* m_error_title{nullptr};
    wxStaticText* m_error_detail{nullptr};
    Button*       m_retry_button{nullptr};

    wxTimer  m_handshake_timer;
    wxString m_page_url;
    wxString m_last_load_error;
};

} // namespace Slic3r::GUI::JusPrin
