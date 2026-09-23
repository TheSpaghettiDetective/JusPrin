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
#include <vector>

namespace Slic3r::GUI { class Plater; }
namespace Slic3r::GUI::JusPrin::Workspace { class SpoolStore; }

namespace Slic3r::GUI::JusPrin::PrinterSetup {

class PrinterPanel : public wxPanel, private IConversationHost
{
public:
    struct Callbacks
    {
        // Back, and everything else that ends the session.
        std::function<void()> closed;
        // A printer was saved or changed; Home's list is out of date.
        // `added` names a printer this session just added, or is empty.
        std::function<void(const std::string& added)> printers_changed;
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
    // Ends the session and tells the owner, as Back does.
    void close();

    void apply_appearance(bool dark);

    // What the panel's page draws above and inside its thread. The harness
    // reads it to see the session without driving the page.
    nlohmann::json session_json() const { return m_conversation->state_json(); }
    // For the shell harness: the open session's host, and the thread it keeps.
    Agent::AgentHost*                host();
    const Agent::ProjectPersistence* persistence() const { return m_persistence.get(); }
    // The page itself, so the harness can tap what the person would.
    AgentWebView* web_view() const { return m_web_view.get(); }
    // Whether the page has written the model's instructions for this session.
    bool instructions_ready() const { return !m_conversation->profile().instructions.empty(); }

private:
    // IConversationHost
    std::string post_note(const std::string& text) override;
    std::string post_opening(const std::string& text) override;
    void        start_turn() override;
    void session_changed() override;
    void profile_changed() override;
    void close_panel() override;
    void printers_changed(const std::string& added) override;
    std::optional<std::string> take_credential(const std::string& action_id) override;

    void build_runtime();
    void tear_down_runtime();
    void on_pump(wxTimerEvent& event);
    void on_release_retired(wxTimerEvent& event);

    // A torn-down runtime whose web view cannot be destroyed yet; see
    // tear_down_runtime. The view goes first: its host holds the document.
    struct RetiredRuntime
    {
        std::unique_ptr<Agent::ProjectPersistence> persistence;
        std::unique_ptr<AgentWebView>              web_view;
    };

    const ShellTheme&      m_theme;
    bool                   m_dark{false};
    Workspace::IWorkspace& m_workspace;
    Callbacks              m_callbacks;

    std::unique_ptr<IPrinterBackend>          m_backend;
    std::unique_ptr<PrinterConversation>      m_conversation;
    std::unique_ptr<Agent::ProjectPersistence> m_persistence;
    std::unique_ptr<AgentWebView>             m_web_view;
    std::vector<RetiredRuntime>               m_retired;
    wxTimer                                   m_pump;
    wxTimer                                   m_release_timer;
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
