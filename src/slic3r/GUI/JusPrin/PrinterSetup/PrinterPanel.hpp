#pragma once

// A temporary settings conversation covering the shell workspace.
//
// Printer tasks bring their own printer tools; filament help uses the regular
// workspace tools. Both use a fresh in-memory document and agent service, so
// neither borrows the saved project conversation.

#include "PrinterConversation.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentWebView.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"

#include <wx/panel.h>
#include <wx/timer.h>

#include <functional>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r::GUI { class Plater; }

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
        // Called after the temporary filament chat closes, before the native
        // material settings dialog is shown.
        std::function<void(std::size_t)> open_filament_settings;
    };

    PrinterPanel(wxWindow*              parent,
                 const ShellTheme&      theme,
                 bool                   dark,
                 Workspace::IWorkspace& workspace,
                 Plater&                plater,
                 Callbacks              callbacks);
    ~PrinterPanel() override;

    // Opens a fresh session. Opening it again replaces the session: no
    // history carries over, by design.
    void open(ConversationMode mode, const std::string& printer_name = {});
    // A filament's chat, about the preset `preset` in `slot`; `shown` is what
    // the header calls it.
    void open_filament(std::size_t slot, const std::string& preset, const std::string& shown, const wxString& return_label);
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
    bool instructions_ready() const
    {
        return m_task == Task::Printer ? !m_conversation->profile().instructions.empty() : !m_filament_instructions.empty();
    }

private:
    // IConversationHost
    std::string post_note(const std::string& text) override;
    std::string post_opening(const std::string& text) override;
    void        start_turn() override;
    void session_changed() override;
    void profile_changed() override;
    void close_panel() override;
    void printers_changed(const std::string& added) override;
    void build_runtime();
    void tear_down_runtime();
    void on_pump(wxTimerEvent& event);
    void on_release_retired(wxTimerEvent& event);
    bool handle_printer_page_message(const std::string& type, const nlohmann::json& payload);
    bool handle_filament_page_message(const std::string& type, const nlohmann::json& payload);
    std::optional<Agent::ToolError> filament_preflight(Agent::ToolHandler handler, const Agent::ToolActivity& activity) const;
    Agent::AgentSessionProfile      filament_profile() const;
    nlohmann::json                  filament_session_json() const;
    void close_to_printer_settings();
    void close_to_filament_settings();

    // A torn-down runtime whose web view cannot be destroyed yet; see
    // tear_down_runtime. The view goes first: its host holds the document.
    struct RetiredRuntime
    {
        std::unique_ptr<Agent::ProjectPersistence> persistence;
        std::unique_ptr<AgentWebView>              web_view;
    };

    const ShellTheme&      m_theme;
    bool                   m_dark{false};
    enum class Task { Printer, Filament };
    Task                   m_task{Task::Printer};
    bool                   m_closing{false};
    std::size_t            m_filament_slot{0};
    std::string            m_filament_name;
    // The filament preset the chat is about, by name; a copy saved in its
    // place from then on.
    std::string            m_filament_preset;
    // The filament chat's instructions, as its page wrote them.
    std::string            m_filament_instructions;
    wxString               m_return_label;
    std::function<void()>  m_after_close;
    Workspace::IWorkspace& m_workspace;
    Callbacks              m_callbacks;

    std::unique_ptr<IPrinterBackend>          m_backend;
    std::unique_ptr<PrinterConversation>      m_conversation;
    std::unique_ptr<Agent::ProjectPersistence> m_persistence;
    std::unique_ptr<AgentWebView>             m_web_view;
    // How the session's settings calls settle, for the conversation to follow
    // a printer saved as a copy. Dropped before the runtime it listens to.
    Agent::ToolActivitySubscription           m_settled;
    std::vector<RetiredRuntime>               m_retired;
    wxTimer                                   m_pump;
    wxTimer                                   m_release_timer;
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
