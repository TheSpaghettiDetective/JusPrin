// First: slic3r/GUI/I18N.hpp defines _L only while the _ macro is still
// undefined, so it has to precede any header that brings one in.
#include "slic3r/GUI/I18N.hpp"

#include "PrinterPanel.hpp"

#include "OrcaPrinterBackend.hpp"
#include "PrinterCatalog.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentConfiguration.hpp"

#include <wx/sizer.h>

#include <optional>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

namespace {

// The same pacing the shell gives the docked panel's host.
constexpr int kPumpIntervalMs = 33;
constexpr int kPumpTimerId    = wxID_HIGHEST + 1803;

// How often a torn-down web view that could not be destroyed yet is looked at
// again.
constexpr int kReleaseRetryMs = 50;
constexpr int kReleaseTimerId = wxID_HIGHEST + 1804;

} // namespace

PrinterPanel::PrinterPanel(wxWindow*              parent,
                           const ShellTheme&      theme,
                           bool                   dark,
                           Workspace::IWorkspace& workspace,
                           Plater&                plater,
                           Workspace::SpoolStore* spools,
                           Callbacks              callbacks)
    : wxPanel(parent, wxID_ANY)
    , m_theme(theme)
    , m_dark(dark)
    , m_workspace(workspace)
    , m_callbacks(std::move(callbacks))
    , m_backend(std::make_unique<OrcaPrinterBackend>(plater, PrinterCatalog::load(Slic3r::resources_dir()), spools))
    // The cast is made here, inside the class, because this panel keeps its
    // conversation-host side private.
    , m_conversation(new PrinterConversation(*m_backend, static_cast<IConversationHost&>(*this)))
    , m_pump(this, kPumpTimerId)
    , m_release_timer(this, kReleaseTimerId)
{
    SetSizer(new wxBoxSizer(wxVERTICAL));
    Bind(wxEVT_TIMER, &PrinterPanel::on_pump, this, kPumpTimerId);
    Bind(wxEVT_TIMER, &PrinterPanel::on_release_retired, this, kReleaseTimerId);
}

PrinterPanel::~PrinterPanel()
{
    m_pump.Stop();
    m_release_timer.Stop();
}

void PrinterPanel::build_runtime()
{
    // Its own document, never the project's: a printer conversation is not
    // part of the project it happens to be opened over, and is gone when the
    // panel closes.
    Agent::ProjectPersistence::Config storage;
    storage.in_memory = true;
    m_persistence     = std::make_unique<Agent::ProjectPersistence>(m_workspace, std::move(storage));

    // Its own agent service too: one turn may be in flight per service, and
    // the docked panel's belongs to the project's conversation.
    Agent::AgentRuntime runtime = Agent::load_agent_runtime(wxGetApp().app_config);
    m_web_view = std::make_unique<AgentWebView>(this, m_theme, m_workspace, *m_persistence, runtime.availability,
                                                std::move(runtime.service), runtime.setup, AgentPageMode::PrinterPanel);
    m_web_view->apply_appearance(m_dark);
    m_web_view->SetName(_L("Printer conversation"));
    GetSizer()->Add(m_web_view.get(), 1, wxEXPAND);
    Layout();

    Agent::AgentHost& host = m_web_view->host();
    host.set_session_profile(m_conversation->profile());
    host.set_session_state_provider([this] { return m_conversation->state_json(); });
    host.set_page_message_handler([this](const std::string& type, const nlohmann::json& payload) {
        return m_conversation->handle_page_message(type, payload);
    });
    host.set_session_tool_executor([this](Agent::ToolHandler handler, const Agent::ToolActivity& activity) {
        return m_conversation->execute_tool(handler, activity);
    });
    host.set_session_tool_preflight([this](Agent::ToolHandler handler, Agent::ToolActivity& activity) {
        return m_conversation->preflight_tool(handler, activity);
    });
    host.set_session_tool_output([this](const Agent::ToolActivity& activity) { return m_conversation->tool_output(activity); });
    // Setting the agent up in here is the same act as setting it up anywhere
    // else; the rest of the shell has to look again afterwards.
    host.set_setup_completed_listener([this] {
        if (m_callbacks.agent_configured)
            m_callbacks.agent_configured();
        if (m_web_view)
            m_web_view->host().set_session_profile(m_conversation->profile());
    });
    m_pump.Start(kPumpIntervalMs);
}

void PrinterPanel::tear_down_runtime()
{
    m_pump.Stop();
    if (!m_web_view)
        return;
    GetSizer()->Detach(m_web_view.get());

    // On macOS a new web view installs its script message handler from a
    // CallAfter (WebView::CreateWebView), and wx waits for WebKit's reply to
    // that install inside a nested event loop, with the app's
    // is_adding_script_handler() flag set until it returns. A view destroyed
    // during that wait is called back by WebKit after it is gone, and the app
    // crashes. So while any view is mid-install, this runtime is hidden, cut
    // off from the session, and destroyed once the install is over -- the same
    // wait GUI_App::run_wizard makes.
    if (wxGetApp().is_adding_script_handler()) {
        m_web_view->Hide();
        Agent::AgentHost& host = m_web_view->host();
        host.set_session_state_provider({});
        host.set_page_message_handler({});
        host.set_session_tool_executor({});
        host.set_setup_completed_listener({});
        m_retired.push_back({std::move(m_persistence), std::move(m_web_view)});
        m_release_timer.StartOnce(kReleaseRetryMs);
        return;
    }

    // Deleted here rather than handed to wx: the host inside it holds this
    // session's document by reference, and that document goes next. Both
    // callers are outside the view's own event handling -- open() comes from
    // the shell, close() from a CallAfter -- which is what makes this safe.
    m_web_view.reset();
    m_persistence.reset();
}

void PrinterPanel::on_release_retired(wxTimerEvent&)
{
    if (wxGetApp().is_adding_script_handler()) {
        m_release_timer.StartOnce(kReleaseRetryMs);
        return;
    }
    m_retired.clear();
}

void PrinterPanel::open(ConversationMode mode, const std::string& printer_name)
{
    // Every opening is a new session: no history carries over, so the runtime
    // that held the last one goes first.
    tear_down_runtime();
    m_conversation->start(mode, printer_name);
    // The page writes the opening line and asks for it to be posted once it
    // has loaded (printer_opening).
    build_runtime();
    Show();
}

void PrinterPanel::close(const PinnedFacts* added)
{
    Hide();
    // Copied now: tear_down_runtime destroys the conversation that owns the
    // facts `added` points at, and that runs before the deferred callback
    // below reads them.
    const std::optional<PinnedFacts> pending = added != nullptr ? std::optional<PinnedFacts>(*added) : std::nullopt;
    // The runtime is destroyed from the page's own message handler, so it
    // cannot be torn down inside a call its own host is still on the stack
    // for -- the trap the add-printer dialog documented.
    CallAfter([this, pending] {
        tear_down_runtime();
        if (m_callbacks.closed)
            m_callbacks.closed(pending ? &*pending : nullptr);
    });
}

Agent::AgentHost* PrinterPanel::host() { return m_web_view ? &m_web_view->host() : nullptr; }

void PrinterPanel::apply_appearance(bool dark)
{
    m_dark = dark;
    if (m_web_view)
        m_web_view->apply_appearance(dark);
}

void PrinterPanel::on_pump(wxTimerEvent&)
{
    if (!m_web_view)
        return;
    Agent::AgentHost& host = m_web_view->host();
    host.pump_stream();
    host.pump_tools();
    host.pump_setup();
}

// -- What the conversation asks of its panel ---------------------------------

std::string PrinterPanel::post_note(const std::string& text)
{
    return m_web_view ? m_web_view->host().post_note(text) : std::string();
}

std::string PrinterPanel::post_opening(const std::string& text)
{
    return m_web_view ? m_web_view->host().post_assistant_message(text) : std::string();
}

void PrinterPanel::start_turn()
{
    if (m_web_view)
        m_web_view->host().start_turn();
}

void PrinterPanel::session_changed()
{
    if (!m_web_view)
        return;
    // A change to the printer is also a change to what the model is told
    // about it.
    m_web_view->host().set_session_profile(m_conversation->profile());
    m_web_view->host().send_page_envelope(Agent::Protocol::kPrinterSession, m_conversation->state_json());
}

void PrinterPanel::profile_changed()
{
    if (m_web_view)
        m_web_view->host().set_session_profile(m_conversation->profile());
}

void PrinterPanel::close_panel(const PinnedFacts* added) { close(added); }

void PrinterPanel::printers_changed(const PinnedFacts* added)
{
    if (m_callbacks.printers_changed)
        m_callbacks.printers_changed(added);
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
