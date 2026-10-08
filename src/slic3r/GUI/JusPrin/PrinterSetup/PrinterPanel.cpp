// First: slic3r/GUI/I18N.hpp defines _L only while the _ macro is still
// undefined, so it has to precede any header that brings one in.
#include "slic3r/GUI/I18N.hpp"

#include "PrinterPanel.hpp"

#include "OrcaPrinterBackend.hpp"
#include "PrinterCatalog.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentConfiguration.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentProtocol.hpp"
#include "slic3r/GUI/JusPrin/Printers/PrinterNames.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <boost/algorithm/string/predicate.hpp>

#include <wx/sizer.h>

#include <optional>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

namespace {

// The same pacing the shell gives the docked panel's host.
constexpr int kPumpIntervalMs = 33;
// The filament chat's instructions are a few kilobytes; far more is not a
// prompt the page meant to send.
constexpr std::size_t kFilamentInstructionsLimit = 64 * 1024;
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
                           Callbacks              callbacks)
    : wxPanel(parent, wxID_ANY)
    , m_theme(theme)
    , m_dark(dark)
    , m_workspace(workspace)
    , m_callbacks(std::move(callbacks))
    , m_backend(std::make_unique<OrcaPrinterBackend>(plater, PrinterCatalog::load(Slic3r::resources_dir())))
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
    // Its own document, never the project's: task help is gone when the
    // panel closes.
    Agent::ProjectPersistence::Config storage;
    storage.in_memory = true;
    m_persistence     = std::make_unique<Agent::ProjectPersistence>(m_workspace, std::move(storage));

    // Its own agent service too: one turn may be in flight per service, and
    // the docked panel's belongs to the project's conversation.
    Agent::AgentRuntime runtime = Agent::load_agent_runtime(wxGetApp().app_config);
    m_web_view = std::make_unique<AgentWebView>(this, m_theme, m_workspace, *m_persistence, runtime.availability,
                                                std::move(runtime.service), runtime.setup,
                                                m_task == Task::Printer ? AgentPageMode::PrinterPanel : AgentPageMode::Conversation);
    m_web_view->apply_appearance(m_dark);
    m_web_view->SetName(m_task == Task::Printer ? _L("Printer conversation") : _L("Filament conversation"));
    GetSizer()->Add(m_web_view.get(), 1, wxEXPAND);
    Layout();

    Agent::AgentHost& host = m_web_view->host();
    if (m_task == Task::Printer) {
        host.set_session_profile(m_conversation->profile());
        host.set_session_state_provider([this] { return m_conversation->state_json(); });
        host.set_page_message_handler([this](const std::string& type, const nlohmann::json& payload) {
            return handle_printer_page_message(type, payload);
        });
        host.set_session_tool_executor([this](Agent::ToolHandler handler, const Agent::ToolActivity& activity) {
            return m_conversation->execute_tool(handler, activity);
        });
        host.set_session_tool_preflight([this](Agent::ToolHandler handler, Agent::ToolActivity& activity) {
            return m_conversation->preflight_tool(handler, activity);
        });
        host.set_session_tool_output([this](const Agent::ToolActivity& activity) { return m_conversation->tool_output(activity); });
        m_settled = host.tools().subscribe([this](const Agent::ToolActivity& activity) { m_conversation->tool_settled(activity); });
        m_web_view->set_error_fallback_actions(
            _L("Back"), m_conversation->printer_name().empty() ? _L("Browse the full printer list") : _L("Open printer settings"),
            [this] { close(); },
            [this] {
                if (m_conversation->printer_name().empty())
                    m_conversation->handle_page_message(Agent::Protocol::kPrinterAction, {{"action", "manual_setup"}});
                else
                    close_to_printer_settings();
            });
    } else {
        // Its own instructions and the settings tools alone: the chat is
        // about one filament preset, not about the open project, which the
        // project assistant's instructions would steer it toward.
        host.set_session_profile(filament_profile());
        host.set_session_state_provider([this] { return filament_session_json(); });
        host.set_navigation_state_provider([this] {
            return nlohmann::json{{"focused", true}, {"returnLabel", m_return_label.ToStdString()}};
        });
        host.set_page_message_handler([this](const std::string& type, const nlohmann::json& payload) {
            return handle_filament_page_message(type, payload);
        });
        m_web_view->set_error_fallback_actions(m_return_label, _L("Open filament settings"),
                                               [this] { close(); }, [this] { close_to_filament_settings(); });
        host.set_session_tool_preflight([this](Agent::ToolHandler handler, Agent::ToolActivity& activity) {
            return filament_preflight(handler, activity);
        });
        // A copy saved in the preset's place takes its slot; the chat is
        // about the copy from then on.
        m_settled = host.tools().subscribe([this](const Agent::ToolActivity& activity) {
            if (activity.tool != "settings_apply_patch" || activity.state != Agent::ToolState::Succeeded)
                return;
            const std::string saved_as = nlohmann::json::parse(activity.result_json).value("savedAs", std::string());
            if (saved_as.empty() || saved_as == m_filament_preset)
                return;
            m_filament_preset = saved_as;
            // The page writes the instructions again for the copy.
            CallAfter([this] {
                if (m_web_view)
                    m_web_view->host().refresh_page_state();
            });
        });
        host.post_assistant_message("I can help with settings for " +
                                    (m_filament_name.empty() ? "the filament" : m_filament_name) +
                                    " in slot " + std::to_string(m_filament_slot + 1) +
                                    ". What would you like to change?");
    }
    // Setting the agent up in here is the same act as setting it up anywhere
    // else; the rest of the shell has to look again afterwards.
    host.set_setup_completed_listener([this] {
        if (m_callbacks.agent_configured)
            m_callbacks.agent_configured();
        if (m_web_view)
            m_web_view->host().set_session_profile(m_task == Task::Printer ? m_conversation->profile() : filament_profile());
    });
    m_pump.Start(kPumpIntervalMs);
}

void PrinterPanel::tear_down_runtime()
{
    m_pump.Stop();
    m_settled.reset();
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
    m_task = Task::Printer;
    m_closing = false;
    m_after_close = {};
    m_conversation->start(mode, printer_name);
    // The page writes the opening line and asks for it to be posted once it
    // has loaded (printer_opening).
    build_runtime();
    Show();
}

void PrinterPanel::open_filament(std::size_t slot, const std::string& preset, const std::string& shown, const wxString& return_label)
{
    tear_down_runtime();
    m_task = Task::Filament;
    m_closing = false;
    m_after_close = {};
    m_filament_slot   = slot;
    m_filament_name   = shown;
    m_filament_preset = preset;
    m_filament_instructions.clear();
    m_return_label = return_label;
    build_runtime();
    Show();
}

void PrinterPanel::close()
{
    if (m_closing)
        return;
    m_closing = true;
    Hide();
    // The runtime is closed from the page's own message handler or from a
    // tool its host is running, so it cannot be torn down inside a call its
    // own host is still on the stack for -- the trap the add-printer dialog
    // documented.
    CallAfter([this] {
        tear_down_runtime();
        if (m_callbacks.closed)
            m_callbacks.closed();
        auto after_close = std::move(m_after_close);
        if (after_close)
            after_close();
    });
}

bool PrinterPanel::handle_printer_page_message(const std::string& type, const nlohmann::json& payload)
{
    if (type == Agent::Protocol::kPrinterAction && payload.is_object() &&
        payload.value("action", "") == "open_printer_settings" && !m_conversation->printer_name().empty()) {
        close_to_printer_settings();
        return true;
    }
    return m_conversation->handle_page_message(type, payload);
}

// A filament's chat changes that filament's settings, as a printer's chat
// changes its printer's, and saves every change it makes: the chat is gone
// when the panel closes, and an unsaved edit would outlive it unexplained.
// Its other tools are the project assistant's.
std::optional<Agent::ToolError> PrinterPanel::filament_preflight(Agent::ToolHandler handler, const Agent::ToolActivity& activity) const
{
    using Agent::ToolHandler;
    if (handler != ToolHandler::SettingsSearch && handler != ToolHandler::SettingsGet &&
        handler != ToolHandler::SettingsPreviewPatch && handler != ToolHandler::SettingsApplyPatch)
        return std::nullopt;
    const nlohmann::json arguments = nlohmann::json::parse(activity.arguments_json);
    if (arguments.value("scope", std::string()) != "filament" ||
        arguments.at("target").value("preset", std::string()) != m_filament_preset)
        return Agent::ToolError{"not_in_this_conversation", "This chat changes the settings of one filament: pass scope filament "
                                                            "and target.preset \"" + m_filament_preset + "\"."};
    if (handler == ToolHandler::SettingsApplyPatch && !arguments.contains("persistAs"))
        return Agent::ToolError{"not_saved", "Every change in this chat is saved: pass persistAs \"" + m_filament_preset +
                                             "\", or the copy's name a read_only_preset issue gives."};
    return std::nullopt;
}

Agent::AgentSessionProfile PrinterPanel::filament_profile() const
{
    Agent::AgentSessionProfile profile;
    profile.tool_names                 = PrinterConversation::settings_tools();
    profile.instructions               = m_filament_instructions;
    profile.include_workspace          = false;
    profile.notes_in_context           = true;
    return profile;
}

// The facts the page writes the instructions from: the preset as it is now,
// by its name, and for one OrcaSlicer ships, the copy a change is saved as.
nlohmann::json PrinterPanel::filament_session_json() const
{
    const PresetCollection& filaments = wxGetApp().preset_bundle->filaments;
    const Preset*           preset    = filaments.find_preset(m_filament_preset, false);

    if (preset == nullptr)
        throw std::logic_error("The filament chat is about a preset that is not there: " + m_filament_preset);
    const auto*    types   = preset->config.option<ConfigOptionStrings>("filament_type");
    nlohmann::json session = {{"kind", "filament"},
                              {"slot", m_filament_slot + 1},
                              {"preset", preset->name},
                              {"shown", m_filament_name},
                              {"material", types != nullptr && !types->values.empty() ? types->values.front() : std::string()},
                              {"stock", preset->is_system}};
    if (preset->is_system)
        session["copyName"] = Printers::copy_name(preset->name, [&filaments](const std::string& candidate) {
            return std::any_of(filaments.begin(), filaments.end(),
                               [&candidate](const Preset& other) { return boost::iequals(other.name, candidate); });
        });
    return session;
}

bool PrinterPanel::handle_filament_page_message(const std::string& type, const nlohmann::json& payload)
{
    if (type == Agent::Protocol::kFilamentInstructions && payload.is_object()) {
        // Bounded: the page's words become every request's system prompt.
        const std::string text = payload.value("text", std::string());
        if (text.size() <= kFilamentInstructionsLimit && text != m_filament_instructions) {
            m_filament_instructions = text;
            m_web_view->host().set_session_profile(filament_profile());
        }
        return true;
    }
    if (type != Agent::Protocol::kShellAction || !payload.is_object())
        return false;
    const std::string action = payload.value("action", "");
    if (action == "return_to_workspace")
        close();
    else if (action == "open_filament_settings")
        close_to_filament_settings();
    else
        return false;
    return true;
}

void PrinterPanel::close_to_printer_settings()
{
    if (m_closing)
        return;
    const std::string name = m_conversation->printer_name();
    m_after_close = [this, name] {
        m_backend->open_printer_settings(name);
        if (m_callbacks.printers_changed)
            m_callbacks.printers_changed({});
    };
    close();
}

void PrinterPanel::close_to_filament_settings()
{
    if (m_closing)
        return;
    const std::size_t slot = m_filament_slot;
    m_after_close = [this, slot] {
        if (m_callbacks.open_filament_settings)
            m_callbacks.open_filament_settings(slot);
    };
    close();
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
    if (m_task == Task::Printer)
        m_conversation->tick(std::chrono::steady_clock::now());
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

void PrinterPanel::close_panel() { close(); }

void PrinterPanel::printers_changed(const std::string& added)
{
    if (m_callbacks.printers_changed)
        m_callbacks.printers_changed(added);
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
