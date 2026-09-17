#pragma once

// The printer conversation, in place of Home's printers column.
//
// It is the Prepare screen's panel in shape -- the same page, thread and
// composer -- with a printer session behind it instead of the project's: its
// own AgentHost, its own agent service, and an in-memory document, so nothing
// said here is written to the open project. Every opening is a new session,
// so the whole runtime is built when the panel opens and torn down when it
// closes.

#include "PrinterConversation.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentWebView.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"

#include <wx/panel.h>
#include <wx/timer.h>

#include <functional>
#include <memory>
#include <string>

namespace Slic3r::GUI { class Plater; }
namespace Slic3r::GUI::JusPrin::Workspace { class SpoolStore; }

namespace Slic3r::GUI::JusPrin::PrinterSetup {

class PrinterPanel : public wxPanel, private IConversationHost
{
public:
    struct Callbacks
    {
        // "‹ Printers", and everything else that ends the session.
        std::function<void()> closed;
        // A printer was saved or changed; Home's list is out of date.
        std::function<void()> printers_changed;
        // The person set the agent up in here, so the rest of the shell has
        // to look again at how it is configured.
        std::function<void()> agent_configured;
    };

    PrinterPanel(wxWindow*              parent,
                 const ShellTheme&      theme,
                 bool                   dark,
                 Workspace::IWorkspace& workspace,
                 Plater&                plater,
                 Workspace::SpoolStore* spools,
                 Callbacks              callbacks);
    ~PrinterPanel() override;

    // Opens a fresh session. Opening it again replaces the session: no
    // history carries over, by design.
    void open(ConversationMode mode, const std::string& printer_name = {});
    // Ends the session and tells the owner, as "‹ Printers" does.
    void close();

    void apply_appearance(bool dark);

    // What the panel's page draws above and inside its thread. The harness
    // reads it to see the session without driving the page.
    nlohmann::json session_json() const { return m_conversation->state_json(); }

private:
    // IConversationHost
    void post_note(const std::string& text) override;
    void ask_agent(const std::string& prompt) override;
    void session_changed() override;
    void close_panel() override;
    void printers_changed() override;

    void build_runtime();
    void tear_down_runtime();
    void on_pump(wxTimerEvent& event);

    const ShellTheme&      m_theme;
    bool                   m_dark{false};
    Workspace::IWorkspace& m_workspace;
    Callbacks              m_callbacks;

    std::unique_ptr<IPrinterBackend>          m_backend;
    std::unique_ptr<PrinterConversation>      m_conversation;
    std::unique_ptr<Agent::ProjectPersistence> m_persistence;
    std::unique_ptr<AgentWebView>             m_web_view;
    wxTimer                                   m_pump;
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
