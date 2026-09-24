// Runs the complete native application and verifies the production JusPrin
// shell through Phase 6: installation, Prepare/Slice/Print, the typed
// Agent bridge and tools, persistence, manufacturing history, resize
// and project replacement, fallback, and restoration of stock presentation.
//
// Modes:
//   --dark-ui / --light-ui  process-only appearance for visual checks. macOS
//              sets the Cocoa appearance; Windows writes dark_color_mode into
//              the run's own config, the only input GUI_App::dark_mode reads
//              there (the registry read in check_dark_mode is compiled out).
//   (default)  automated shell checks, exits with the result
//   --stock    automated stock-mode checks (shell disabled via app config)
//   --manual   installs nothing extra and leaves the app open for a human
//   --manual-live-agent
//              leaves the isolated JusPrin shell open with the OpenAI
//              provider enabled for hands-on testing
//   --manual-unconfigured
//              leaves the isolated JusPrin shell open with no Agent service
//              configured at all -- the dock's not-set-up empty state
//   --live-agent
//              uses OPENAI_API_KEY and verifies live context/attachment use,
//              reload recovery, rejection, native approval/mutation, and the
//              model follow-ups
//   --mcp     real TCP discovery/read, approval/rejection, stale proposal,
//              disconnect, reload, and native undo using the shared runtime
//   --mcp-bridge  same native scenario through a persistent stdio subprocess
//   --mcp-setup   native setup-command lifetime, output and argument checks;
//                 uses this harness as a fixture, never edits client config
//   --manual-mcp <fixture-directory>
//                 leaves a two-plate native fixture open for real MCP clients;
//                 exposes Orca's sidebar for native-edit/stale-revision checks;
//                 reuse the directory to test restarts without reconfiguration
//   --header-visual <fixture-directory>
//                 same fixture without the expert sidebar, with the header menu open
//   --live-agent-unavailable
//              selects OpenAI with consent withheld and verifies that the
//              application stays usable without silently selecting the mock
//   --slice-all-cold
//              guards Slice all on a multi-plate project before the Preview
//              canvas has ever rendered — the conditions of upstream crash
//              #15116 (null all-plates stats item; fixed upstream by
//              83723e2a7b). Observed 2026-08-31: it PASSES on this tree
//              because the startup color-mode sync also runs
//              _init_select_plate_toolbar on the Preview canvas before any
//              render. Kept as a guard in case that incidental init path
//              changes; run it alongside the default and --stock modes.
//              The synchronous Prepare-only slice command made this reachable
//              on 2026-09-04. The no-auto-preview policy now also leaves the
//              unopened Preview's plate selection unchanged.
//   --recomputing-capture <output-directory>
//              slices the fixture, scales the cube to 120 mm and re-slices,
//              asserts the setup card reads "re-slicing…" while the slice
//              runs, and writes recomputing-agent-pane.png and
//              recomputing-shell.png to the directory (handoff item 6)
//   --timeline-capture <output-directory>
//              records a build, asks a question the Agent answers without a
//              change, mirrors the object three times and edits a print
//              setting by hand, asserts the thread's change rows and the
//              "Answered · nothing changed" marker, and writes
//              timeline-agent-pane-<light|dark>.png (revision-timeline B10)
//   --printer-live
//              needs OPENAI_API_KEY: the printer-agent-tools handoff's worked
//              examples through the real printer panel and the live model --
//              words and taps sent as the page sends them, every exchange
//              printed as HARNESS LIVE lines, and the mechanical outcomes
//              checked (cards drawn or not, the change card, the saved
//              nozzle, Undo). The key is written to this run's throwaway
//              app config, where the panel reads it.
//   --printer-connect-capture <output-directory>
//              needs OPENAI_API_KEY and macOS: connects a Klipper printer
//              through the real printer panel and the live model, against
//              print hosts on loopback ports, and writes a PNG of the panel
//              at each state of the connect card (waiting, a question asked
//              meanwhile, failed, cancelled, connected) and of Home after.
//              The pictures are WebKit's own snapshots, which need no Screen
//              Recording permission
//   --manual-tool-strip
//              leaves the shell open on the two-plate fixture with an object
//              selected, for hands-on testing of the canvas tool strip
//   --tool-strip-capture <output-directory>
//              clicks the Prepare canvas's tool strip with real pointer
//              events: toggles Move and Scale, follows the Rotate shortcut,
//              duplicates and undoes, opens More (Windows), orbits and
//              narrows the window; asserts the open tool, selection and
//              instance count at each step, and writes tool-strip-*.png
//   --printer-setup
//              drives the Add a printer modal from the printer menu with the
//              deterministic recognizer: dismissal and scrim lifetime, the
//              choice/Not this one/Start over/correction transitions, adding a
//              shipped but disabled profile, the network state, Set it up
//              myself, and install rollback; then named printers: a second
//              printer of one model, Home's list, rename, remove, and a
//              nozzle change that keeps the printer's own settings

// First: on Windows asio needs winsock2.h ahead of any windows.h.
#include <boost/asio.hpp>

#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/libslic3r.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_Init.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentConfiguration.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentWebView.hpp"
#include "slic3r/GUI/JusPrin/Agent/OpenAIResponsesAgent.hpp"
#include "slic3r/GUI/JusPrin/Agent/ToolRegistry.hpp"
#include "../jusprin_support/DeterministicMockAgent.hpp"
#include "slic3r/GUI/JusPrin/Brand/BrandPalette.hpp"
#include "slic3r/GUI/JusPrin/Canvas/ViewportToolStrip.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
// FindWindowByName and ImGuiWindow: the tool panel is an ImGui window, and its
// rectangle is what the anchor checks read.
#include <imgui/imgui_internal.h>
#include "slic3r/GUI/JusPrin/Mcp/McpRuntime.hpp"
#include "../agent/mcp_test_client.hpp"
#include "mcp_stdio_client.hpp"
#include "slic3r/GUI/JusPrin/Shell/AgentPane.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SettingsSupport.hpp"
#include "slic3r/GUI/JusPrin/Shell/McpSetupCommand.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellController.hpp"
#include "slic3r/GUI/JusPrin/Shell/PrinterSpoolChip.hpp"
#include "slic3r/GUI/JusPrin/Shell/SetupCommands.hpp"
#include "slic3r/GUI/JusPrin/Shell/StatusRow.hpp"
#include "slic3r/GUI/JusPrin/Shell/HeaderControls.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/OrcaPrinterBackend.hpp"
#include "slic3r/GUI/JusPrin/Testing/FakeBambuAgent.hpp"
#include "slic3r/Utils/NetworkAgentFactory.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"

#include <deque>
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterCatalog.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterPanel.hpp"
#include "slic3r/GUI/JusPrin/Printers/NamedPrinters.hpp"
#include "slic3r/GUI/JusPrin/Home/HomeWebView.hpp"
#include "slic3r/GUI/JusPrin/Home/OrcaHomeBackend.hpp"
#include "slic3r/GUI/JusPrin/Shell/SetupCommands.hpp"
#include "slic3r/GUI/WebGuideDialog.hpp"
#include "slic3r/GUI/ParamsDialog.hpp"
#include "slic3r/GUI/ParamsPanel.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Notebook.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"

#include <wx/app.h>
#include <wx/dcmemory.h>
#include <wx/dcscreen.h>
#include <wx/glcanvas.h>
#include <wx/dialog.h>
#include <wx/dnd.h>
#include <wx/hyperlink.h>
#include <wx/webview.h>
#include <wx/process.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

#include <boost/filesystem.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
// For reading this process's real command line as UTF-16; see utf8_argument.
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = boost::filesystem;

#ifdef __APPLE__
void set_harness_appearance(bool dark);
bool snapshot_web_view(void* native_web_view, const char* path);
#endif

namespace Slic3r::GUI::JusPrin {
namespace {

// Labels reach the widgets through _L, which decodes its narrow literal as
// UTF-8 explicitly (I18N.hpp). wxString's own narrow constructor instead
// decodes through the locale's encoding: UTF-8 on macOS, but the ANSI code
// page on Windows, where the three bytes of "…" become three separate
// characters. A lookup written as a bare literal therefore matches on macOS
// and silently misses on Windows -- silently because a missing row reads as
// "the menu does not contain this", which is what several of these checks
// assert. Every name below that carries a non-ASCII character goes through
// here so both sides decode the same way.
wxString ui_name(const char* utf8) { return wxString::FromUTF8(utf8); }

// The same hazard for a name read back out of a widget: menu_row_names hands
// back UTF-8, and letting that std::string convert itself to wxString would
// re-decode it through the locale and undo the round trip on Windows.
wxString ui_name(const std::string& utf8) { return wxString::FromUTF8(utf8.c_str()); }

// A print host on a loopback port, for connection tests without hardware.
// Each request is held for `hold` (until the stand-in goes, when it is
// negative), then either answered as Moonraker's /server/info or hung up on.
class StandInHost
{
public:
    StandInHost(std::chrono::milliseconds hold, bool answer)
        : m_acceptor(m_io, boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0))
        , m_hold(hold)
        , m_answer(answer)
    {
        m_thread = std::thread([this] { serve(); });
    }
    ~StandInHost()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
        }
        m_released.notify_all();
        // Wakes a blocking accept.
        boost::system::error_code ignored;
        boost::asio::ip::tcp::socket wake(m_io);
        wake.connect(m_acceptor.local_endpoint(), ignored);
        m_thread.join();
    }
    std::string address() const { return "http://127.0.0.1:" + std::to_string(m_acceptor.local_endpoint().port()); }
    int requests() const { return m_requests; }

private:
    void serve()
    {
        for (;;) {
            boost::system::error_code error;
            boost::asio::ip::tcp::socket socket(m_io);
            m_acceptor.accept(socket, error);
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_stopping || error)
                return;
            ++m_requests;
            boost::asio::streambuf request;
            lock.unlock();
            boost::asio::read_until(socket, request, "\r\n\r\n", error);
            lock.lock();
            if (m_hold.count() < 0)
                m_released.wait(lock, [this] { return m_stopping; });
            else
                m_released.wait_for(lock, m_hold, [this] { return m_stopping; });
            if (m_answer && !m_stopping) {
                const std::string body = R"({"result":{"klippy_state":"ready"}})";
                const std::string reply = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                          std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                boost::asio::write(socket, boost::asio::buffer(reply), error);
            }
            socket.close(error);
        }
    }

    boost::asio::io_context        m_io;
    boost::asio::ip::tcp::acceptor m_acceptor;
    std::chrono::milliseconds      m_hold;
    bool                           m_answer;
    std::atomic<int>               m_requests{0};
    std::mutex                     m_mutex;
    std::condition_variable        m_released;
    bool                           m_stopping{false};
    std::thread                    m_thread;
};

// A model that answers the first message by calling `tool` with `arguments`,
// and anything after that with a line of text.
class ToolCallingAgent final : public Agent::IAgentService
{
public:
    ToolCallingAgent(std::string tool, nlohmann::json arguments) : m_tool(std::move(tool)), m_arguments(std::move(arguments)) {}

    bool ready() const override { return true; }
    bool busy() const override { return m_active; }
    bool start(const Agent::AgentRequest& request) override
    {
        m_active = true;
        if (request.purpose != Agent::AgentRequest::Purpose::ConversationTitle && !m_called) {
            m_called = true;
            m_events.push_back(Agent::AgentEvent::tool_call({"call-1", Agent::ToolRequest{m_tool, m_arguments.dump()}, true}));
        } else {
            m_events.push_back(Agent::AgentEvent::delta("Checking."));
            m_events.push_back(Agent::AgentEvent::completed());
        }
        return true;
    }
    bool continue_after_tool(const Agent::AgentToolResult&) override
    {
        m_events.push_back(Agent::AgentEvent::delta("Checking."));
        m_events.push_back(Agent::AgentEvent::completed());
        return true;
    }
    void cancel() override
    {
        m_active = false;
        m_events.clear();
    }
    std::optional<Agent::AgentEvent> poll() override
    {
        if (m_events.empty())
            return std::nullopt;
        Agent::AgentEvent event = std::move(m_events.front());
        m_events.pop_front();
        if (event.kind == Agent::AgentEventKind::Completed || event.kind == Agent::AgentEventKind::Failed)
            m_active = false;
        return event;
    }

private:
    std::string                   m_tool;
    nlohmann::json                m_arguments;
    std::deque<Agent::AgentEvent> m_events;
    bool                          m_active{false};
    bool                          m_called{false};
};

// wxString::ToStdString() narrows through that same locale converter and
// returns an empty string when a character will not fit the code page. Names
// compared as std::string are read as UTF-8 for the same reason.
std::string ui_text(const wxString& name) { return name.ToUTF8().data(); }

struct HarnessState
{
    enum class Mode {
        Shell,
        Stock,
        Manual,
        ManualLiveAgent,
        ManualUnconfigured,
        SliceAllCold,
        LiveAgent,
        Mcp,
        McpSetup,
        ManualMcp,
        LiveAgentUnavailable,
        RecomputingCapture,
        TimelineCapture,
        ToolStripCapture,
        ManualToolStrip,
        PrinterSetup,
        PrinterLive
    };

    std::atomic<int>  result{-1};
    std::atomic<bool> stop{false};
    std::shared_ptr<void> runner;
    // Phase 4 added real project saves and reopen on top of two full
    // slices; 300 s was regularly exhausted mid-flow. The
    // warm Slice-all scenario adds up to two more plate slices.
    std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::now() + std::chrono::seconds(900)};
    Mode mode{Mode::Shell};
    bool mcp_bridge{false};
    bool header_visual{false};
    std::optional<bool> dark_appearance;
    fs::path capture_dir;
    // --printer-connect-capture: the connect flow only, pictured.
    bool connect_capture{false};
    std::shared_ptr<JusPrinTest::StdioClient> bridge;
};

Notebook* find_notebook(wxWindow* root, Plater* plater)
{
    if (auto* notebook = dynamic_cast<Notebook*>(root))
        if (notebook->FindPage(plater) == MainFrame::tp3DEditor) return notebook;
    for (wxWindow* child : root->GetChildren())
        if (Notebook* notebook = find_notebook(child, plater))
            return notebook;
    return nullptr;
}

class Scenario final : public std::enable_shared_from_this<Scenario>
{
public:
    Scenario(GUI_App& app, std::shared_ptr<HarnessState> state) : m_app(app), m_state(std::move(state))
    {
        m_poll_handler.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { poll_wait(); });
        m_exit_handler.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
            std::cerr << "HARNESS ERROR main loop still running 30 s after the frame was closed; forcing ExitMainLoop\n";
            m_app.ExitMainLoop();
        });
    }

    void start()
    {
        try {
            m_plater = m_app.plater();
            m_frame  = m_app.mainframe;
            check(m_plater != nullptr && m_frame != nullptr, "application_ready");
            m_notebook = find_notebook(m_frame, m_plater);
            check(m_notebook != nullptr, "notebook_found");
            if (m_plater == nullptr || m_frame == nullptr || m_notebook == nullptr) {
                fail("application widgets missing");
                return;
            }
            // GUI_App::post_init runs from the first idle event after the main
            // loop starts and ends with select_tab(0) (GUI_App.cpp, "if
            // (is_editor()) mainframe->select_tab(size_t(0))"). This scenario
            // starts from a CallAfter, which the loop drains before it idles,
            // so every mode used to drive the shell ahead of post_init, and
            // post_init later undid whatever tab the scenario had chosen --
            // which is how the three visual modes handed over a Home screen
            // while reporting failures=0. On a software renderer the first
            // idle arrived seconds after the fixture had finished slicing.
            wait_until_settled("startup_post_init_complete",
                               [self = shared_from_this()] { self->run_mode(); });
        } catch (const std::exception& error) {
            fail(std::string("exception: ") + error.what());
        } catch (...) {
            fail("unknown exception");
        }
    }

    void run_mode()
    {
        try {
            if (m_state->mode == HarnessState::Mode::Stock) {
                wait_until([this] { return m_plater->canvas3D()->is_initialized() &&
                    m_notebook->GetSelection() == MainFrame::tpHome; }, "stock_startup_ready",
                           [self = shared_from_this()] { self->verify_stock_slice(); });
                return;
            }
            verify_shell_installed();
            verify_agent_pane_follows_page([self = shared_from_this()] { self->run_shell_mode(); });
        } catch (const std::exception& error) {
            fail(std::string("exception: ") + error.what());
        } catch (...) {
            fail("unknown exception");
        }
    }

    void run_shell_mode()
    {
        try {
            if (m_state->mode == HarnessState::Mode::PrinterSetup) {
                verify_printer_setup();
                return;
            }
            if (m_state->mode == HarnessState::Mode::PrinterLive) {
                begin_printer_live();
                return;
            }
            if (m_state->mode == HarnessState::Mode::McpSetup) {
                verify_mcp_setup();
                finish();
                return;
            }
            load_multi_plate_fixture();
            if (m_state->mode == HarnessState::Mode::ManualMcp) {
                verify_canvas_interaction();
                prepare_mcp_slice([self = shared_from_this()] {
                    // Real-client tests need a native setting edit while the
                    // shell's rendered external approval cards remain live.
                    if (self->m_state->header_visual) {
                        installed_shell()->status_row()->show_action_menu();
                    } else {
                        self->m_plater->set_sidebar_available(true);
                        self->m_plater->collapse_sidebar(false);
                        self->check(self->m_plater->sidebar().IsShown(), "mcp_fixture_native_sidebar_visible");
                    }
                    // The checks above fire at the instant of navigation.
                    // Anything deferred -- a queued page change, a CallAfter
                    // from a load handler, a late post_init -- lands after
                    // them, so the tab is asserted once more when the loop
                    // has actually been idle, and the READY line carries
                    // that result.
                    self->wait_until_settled("mcp_fixture_settled", [self] {
                        self->check(self->m_notebook->GetSelection() == MainFrame::tp3DEditor,
                                    "mcp_fixture_tab_survives_settle");
                        self->check(!self->m_plater->is_preview_shown(), "mcp_fixture_view_survives_settle");
                        // The hand-over screen is what gets photographed, so
                        // the page's own error pane is re-checked last.
                        self->check(!installed_shell()->agent_pane()->web_view().bridge_error_shown(),
                                    "mcp_fixture_agent_page_healthy_at_handover");
                        std::cerr << "HARNESS MCP FIXTURE READY failures=" << self->m_failures
                                  << " discovery=" << data_dir() << "/jusprin/mcp.json\n";
                    });
                });
                return;
            }
            if (m_state->mode == HarnessState::Mode::Mcp) {
                verify_canvas_interaction();
                prepare_mcp_slice([self = shared_from_this()] { self->begin_mcp(); });
                return;
            }
            if (m_state->mode == HarnessState::Mode::LiveAgent) {
                verify_canvas_interaction();
                verify_live_agent();
                return;
            }
            if (m_state->mode == HarnessState::Mode::LiveAgentUnavailable) {
                verify_canvas_interaction();
                verify_unavailable_agent();
                return;
            }
            if (m_state->mode == HarnessState::Mode::RecomputingCapture) {
                verify_canvas_interaction();
                wait_for_agent_page("recomputing", [self = shared_from_this()] { self->begin_recomputing_capture(); });
                return;
            }
            if (m_state->mode == HarnessState::Mode::TimelineCapture) {
                verify_canvas_interaction();
                wait_for_agent_page("timeline", [self = shared_from_this()] { self->begin_timeline_capture(); });
                return;
            }
            if (m_state->mode == HarnessState::Mode::ManualToolStrip) {
                verify_canvas_interaction();
                wait_until_settled("manual_tool_strip_ready", [self = shared_from_this()] {
                    std::cerr << "HARNESS MANUAL READY tool-strip failures=" << self->m_failures << '\n';
                });
                return;
            }
            if (m_state->mode == HarnessState::Mode::ToolStripCapture) {
                verify_canvas_interaction();
                wait_until_settled("tool_strip_selection_settled", [self = shared_from_this()] { self->tool_strip_idle(); });
                return;
            }
            if (m_state->mode == HarnessState::Mode::SliceAllCold) {
                wait_for_agent_page("cold", [self = shared_from_this()] { self->begin_slice_all_cold(); });
                return;
            }
            verify_canvas_interaction();
            wait_for_agent_page("shell", [self = shared_from_this()] { self->begin_slice_check(); });
        } catch (const std::exception& error) {
            fail(std::string("exception: ") + error.what());
        } catch (...) {
            fail("unknown exception");
        }
    }

private:
    void verify_stock_slice()
    {
        verify_stock_mode();
        check(m_plater->auto_preview_after_slice(), "stock_keeps_auto_preview_policy");
        load_multi_plate_fixture();
        m_plater->update(true, true);
        wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_SLICE_PLATE));
        m_frame->select_tab(MainFrame::tpPreview);
        wait_until([this] { return m_plater->is_preview_shown() && !m_plater->is_background_process_slicing() &&
            m_plater->get_partplate_list().get_curr_plate()->is_slice_result_valid(); }, "stock_slice_opens_preview",
            [self = shared_from_this()] {
                self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "stock_slice_teardown");
                self->finish();
            });
    }

    void verify_mcp_setup()
    {
        const std::string executable = wxStandardPaths::Get().GetExecutablePath().ToUTF8().data();
        const std::string literal = "Kenny's 打印 path; $NOT_EXPANDED";
        const auto success = run_mcp_setup_command(m_frame, {executable, "--setup-child", "success", literal});
        check(success.success && success.diagnostic.find(literal) != std::string::npos, "mcp_setup_literal_arguments");
        const auto failure = run_mcp_setup_command(m_frame, {executable, "--setup-child", "failure", literal});
        check(!failure.success && failure.diagnostic.find("code 7") != std::string::npos &&
              failure.diagnostic.find("fixture stderr") != std::string::npos, "mcp_setup_cli_failure_visible");
        const auto large = run_mcp_setup_command(m_frame, {executable, "--setup-child", "large", literal});
        check(large.success && large.diagnostic.size() <= 65600, "mcp_setup_output_bounded_without_deadlock");
        wxEvtHandler events;
        wxTimer cancel(&events);
        bool attempted_cancel = false;
        events.Bind(wxEVT_TIMER, [&](wxTimerEvent&) {
            for (auto* window : wxTopLevelWindows) {
                auto* dialog = dynamic_cast<wxDialog*>(window);
                if (dialog && dialog->GetTitle() == "Connecting AI tool") {
                    attempted_cancel = true;
                    wxCommandEvent event(wxEVT_BUTTON, wxID_CANCEL);
                    dialog->GetEventHandler()->ProcessEvent(event);
                }
            }
        });
        cancel.StartOnce(100);
        const auto started = std::chrono::steady_clock::now();
        const auto delayed = run_mcp_setup_command(m_frame, {executable, "--setup-child", "delayed", literal});
        check(attempted_cancel, "mcp_setup_cancel_event_exercised");
        check(delayed.success && std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(800),
              "mcp_setup_monitor_outlives_child_after_cancel_event");

        std::vector<std::string> timeout_modes{"timeout"};
#ifndef _WIN32
        // Windows terminates a process directly; it has no POSIX TERM/KILL escalation.
        timeout_modes.push_back("ignore-term");
#endif
        for (const auto& mode : timeout_modes) {
            wxEvtHandler heartbeat_events;
            wxTimer heartbeat(&heartbeat_events);
            int ticks = 0;
            heartbeat_events.Bind(wxEVT_TIMER, [&](wxTimerEvent&) { ++ticks; });
            heartbeat.Start(50);
            const auto timeout_started = std::chrono::steady_clock::now();
            const auto result = run_mcp_setup_command(m_frame, {executable, "--setup-child", mode, literal});
            heartbeat.Stop();
            const auto elapsed = std::chrono::steady_clock::now() - timeout_started;
            check(!result.success && result.diagnostic.find("Setup timed out") != std::string::npos &&
                  result.diagnostic.find("config may have changed") != std::string::npos,
                  "mcp_setup_" + mode + "_reports_uncertain_config");
            check(elapsed >= std::chrono::seconds(mode == "timeout" ? 30 : 32) &&
                  elapsed < std::chrono::seconds(45), "mcp_setup_" + mode + "_bounded_termination");
            check(ticks > 100, "mcp_setup_" + mode + "_event_loop_responsive");
            const auto marker = result.diagnostic.find("fixture pid ");
            check(marker != std::string::npos, "mcp_setup_" + mode + "_child_started");
            if (marker != std::string::npos) {
                const auto child_pid = std::stol(result.diagnostic.substr(marker + 12));
                check(!wxProcess::Exists(child_pid), "mcp_setup_" + mode + "_child_reaped");
            }
        }
        const auto recovered = run_mcp_setup_command(m_frame, {executable, "--setup-child", "success", literal});
        check(recovered.success, "mcp_setup_succeeds_after_timeouts");
    }

    // --- The printer conversation (--printer-setup) -----------------------
    //
    // The panel takes Home's place, so the checks below drive the shell the
    // way Home's own bridge does and read the session the panel would draw.
    // The conversation itself needs a live agent (--printer-live); what is
    // checked here is the panel around it: that it opens on both paths,
    // states the right printer, and gives Home back when it closes.

    static constexpr const char* kSetupFixturePrinter = "Bambu Lab X1 Carbon 0.4 nozzle";
    // Adding the Neo saves its system profile as a printer named after the
    // model, and that printer is what stays selected afterwards.
    static constexpr const char* kAddedPrinter        = "Creality Ender-3 V2 Neo";
    static constexpr const char* kAddedPrinterProfile = "Creality Ender-3 V2 Neo 0.4 nozzle";

    static std::string selected_printer() { return wxGetApp().preset_bundle->printers.get_selected_preset_name(); }

    // One half of the header's printer chip, by the name that half carries.
    static wxString chip_label(wxWindow* row, const char* half)
    {
        wxWindow* control = wxWindow::FindWindowByName(half, row);
        return control ? control->GetLabel() : wxString();
    }

    void verify_printer_setup()
    {
        check(selected_printer() == kSetupFixturePrinter, "setup_fixture_printer_selected");

        ShellController*            shell = installed_shell();
        Home::HomeWebView*          home  = shell == nullptr ? nullptr : shell->home_view();
        PrinterSetup::PrinterPanel* panel = shell == nullptr ? nullptr : shell->printer_panel();
        if (home == nullptr || panel == nullptr) {
            fail("the shell has no Home view or printer panel");
            return;
        }
        check(!panel->IsShown(), "panel_closed_before_it_is_asked_for");

        // "+ Add printer" on Home, as the page sends it.
        home->backend().add_printer();
        check(panel->IsShown(), "panel_opens_from_add_printer");
        check(home->IsShown(), "panel_opens_on_home");
        const nlohmann::json add = panel->session_json();
        check(add.value("mode", "") == "add" && add.value("printerName", "x").empty(), "panel_adds_with_no_printer_yet");
        check(add["context"].contains("printers") && !add["context"].contains("printer"), "panel_sends_the_printer_list");
        // The page writes every word; the app sends none.
        check(!add.contains("caption") && !add.contains("placeholder") && !add.contains("chips"),
              "panel_sends_facts_not_words");
        const bool tip = std::any_of(add["blocks"].begin(), add["blocks"].end(),
                                     [](const nlohmann::json& block) { return block.value("kind", "") == "tip"; });
        check(tip, "panel_offers_the_photo_path_in_words");

        // A printer to change: the fixture's own profile saved under a name.
        const std::string named = Printers::add_named_printer(*m_plater, "Lab Printer", {});
        home->backend().open_printer_settings("named:" + named);
        check(panel->IsShown(), "panel_opens_from_printer_settings");
        const nlohmann::json change = panel->session_json();
        check(change.value("mode", "") == "change", "panel_changes_the_printer_it_was_opened_for");
        check(change.value("printerName", "") == named && change["context"]["printer"].value("name", "") == named,
              "panel_states_the_printer_by_name");
        check(change["context"]["printer"].value("nozzle", 0.) == 0.4, "panel_states_the_nozzle_it_is_set_up_with");
        verify_nozzle_change_leaves_the_project(named);

        // Back gives Home back.
        panel->close();
        wxYield();
        check(!panel->IsShown(), "panel_closes_back_to_the_printer_list");
        check(Printers::remove_named_printer(*m_plater, nullptr, named).empty(), "panel_cleanup_removes_the_printer");
        SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
        verify_setup_install_commands();
    }

    // A nozzle changed from Home is a change to that printer, not to the
    // project that happens to be open: the project keeps the printer it has
    // selected and its modified state, and nothing asks the person anything
    // (a dialog would stop this harness where it stands).
    void verify_nozzle_change_leaves_the_project(const std::string& named)
    {
        PresetCollection&              printers = wxGetApp().preset_bundle->printers;
        PrinterSetup::OrcaPrinterBackend backend(*m_plater, PrinterSetup::PrinterCatalog::load(Slic3r::resources_dir()),
                                                 nullptr);
        const auto change = [&](double nozzle) {
            PrinterSetup::ChangePrinterRequest request;
            request.name   = named;
            request.nozzle = nozzle;
            PrinterSetup::SavedPrinter changed;
            return backend.change_printer(request, changed);
        };
        const auto parent = [&] {
            const Preset* profile = printer_profile(named);
            return profile == nullptr ? std::string() : profile->inherits();
        };
        const auto edited_nozzle = [&] {
            const auto* nozzle = printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter");
            return nozzle == nullptr || nozzle->values.empty() ? 0. : nozzle->values.front();
        };

        // Another printer is the project's.
        SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
        const bool dirty = m_plater->is_project_dirty();
        check(change(0.6).empty() && parent() == "Bambu Lab X1 Carbon 0.6 nozzle", "nozzle_change_moves_the_printer_to_its_size");
        check(selected_printer() == kSetupFixturePrinter, "nozzle_change_keeps_the_projects_printer");
        check(m_plater->is_project_dirty() == dirty, "nozzle_change_keeps_the_projects_modified_state");
        check(edited_nozzle() == 0.4, "nozzle_change_leaves_the_projects_printer_settings");
        // Undo is the same operation back.
        check(change(0.4).empty() && parent() == kSetupFixturePrinter, "nozzle_undo_moves_it_back");

        // The printer is the project's, with nothing unsaved: the project's
        // copy follows, still selected, still clean.
        SetupCommands::select_printer_preset(*m_plater, named);
        const bool dirty_on_it = m_plater->is_project_dirty();
        check(change(0.6).empty() && selected_printer() == named, "nozzle_change_of_the_projects_printer_keeps_it_selected");
        check(edited_nozzle() == 0.6 && !printers.current_is_dirty(), "nozzle_change_of_the_projects_printer_reaches_it");
        check(m_plater->is_project_dirty() == dirty_on_it, "nozzle_change_of_the_projects_printer_keeps_its_modified_state");

        // Unsaved edits to it are the person's: refused in words, with no
        // prompt, and nothing moved.
        printers.get_edited_preset().config.set_key_value("printer_notes", new ConfigOptionString("unsaved"));
        check(printers.current_is_dirty(), "nozzle_fixture_has_unsaved_printer_edits");
        check(!change(0.4).empty() && parent() == "Bambu Lab X1 Carbon 0.6 nozzle",
              "nozzle_change_refuses_over_unsaved_edits_without_a_prompt");
        printers.discard_current_changes();
        check(change(0.4).empty() && parent() == kSetupFixturePrinter, "nozzle_cleanup_moves_it_back");
        SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
    }

    // --- The worked examples, live (--printer-live) ------------------------
    //
    // The handoff's worked examples through the real panel: its own session,
    // the live model, the Orca backend. Words and taps go in as the page would
    // send them; each exchange is printed for reading, and what can be checked
    // mechanically is.

    struct LiveStep
    {
        std::string            label;
        std::function<void()>  act;   // opens a session, or sends words or a tap
        std::function<bool()>  ready; // when the exchange has settled
        std::function<void()>  check; // then
    };
    std::deque<LiveStep>     m_live_steps;
    std::size_t              m_live_printed{0};
    std::string              m_live_printer;
    std::vector<std::string> m_live_added;
    bool                     m_live_wizard_seen{false};
    std::string              m_live_host_printer;
    std::string              m_live_step; // the running step's label

    // The Orca backend over the harness's own plater, for what the panel's
    // conversation reads through it.
    PrinterSetup::OrcaPrinterBackend& live_backend()
    {
        static PrinterSetup::OrcaPrinterBackend backend(*m_plater, PrinterSetup::PrinterCatalog::load(Slic3r::resources_dir()), nullptr);
        return backend;
    }

    PrinterSetup::PrinterPanel* live_panel() const
    {
        ShellController* shell = installed_shell();
        return shell == nullptr ? nullptr : shell->printer_panel();
    }

    // Opens the panel the way Home and the header do: through the shell, which
    // gives it the whole window.
    void live_show(PrinterSetup::ConversationMode mode, const std::string& printer)
    {
        installed_shell()->open_printer_conversation(mode == PrinterSetup::ConversationMode::Add ? std::string() : printer,
                                                     mode == PrinterSetup::ConversationMode::Connect);
    }

    void live_send(const std::string& type, const nlohmann::json& payload)
    {
        static int next = 0;
        // The model can close the panel (printer_setup_finish) before the
        // script is done with it. The step then fails by name, and the run
        // goes on: live_settled() counts a closed panel as settled, and the
        // next open starts a fresh session.
        Agent::AgentHost* host = live_panel()->host();
        if (host == nullptr) {
            std::string step;
            for (const char c : m_live_step)
                step += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
            check(false, "live_panel_open_for_" + step);
            return;
        }
        host->on_page_message(nlohmann::json{{"protocol", Agent::Protocol::kName},
                                             {"version", Agent::Protocol::kVersion},
                                             {"id", "live-" + std::to_string(++next)},
                                             {"type", type},
                                             {"payload", payload}}
                                  .dump());
    }

    // Whether the app posted a note after this message.
    bool live_note_after(const std::string& message_id) const
    {
        const auto messages = live_messages();
        bool after = false;
        for (const auto& message : messages) {
            if (after && message.role == Agent::MessageRole::Note)
                return true;
            after = after || message.id == message_id;
        }
        return false;
    }

    // The page's opening is the thread's first line, as the agent's.
    void check_live_opening(const std::string& start, const std::string& name)
    {
        const auto messages = live_messages();
        check(!messages.empty() && messages.front().role == Agent::MessageRole::Assistant &&
                  messages.front().text.rfind(start, 0) == 0,
              name);
    }

    // --- Pictures of the live run (PRINTER_LIVE_CAPTURE_DIR) ----------------
    //
    // With the variable set, each capture point writes <name>.png of the
    // panel, through WebKit's own snapshot as --printer-connect-capture does.
    void live_capture(const std::string& name)
    {
        const char* dir = std::getenv("PRINTER_LIVE_CAPTURE_DIR");
        if (dir == nullptr)
            return;
        m_state->capture_dir = dir;
        capture_panel(name);
    }

    void say(const std::string& text)
    {
        static int next = 0;
        live_send("user_message", {{"clientMessageId", "live-c-" + std::to_string(++next)}, {"text", text}});
    }

    std::vector<Agent::ConversationMessage> live_messages() const
    {
        if (live_panel()->host() == nullptr)
            return {};
        const Agent::ProjectPersistence* persistence = live_panel()->persistence();
        if (persistence == nullptr)
            return {};
        return persistence->document().messages(persistence->document().active_conversation_id());
    }

    const std::vector<Agent::ToolActivity>& live_activities() const
    {
        static const std::vector<Agent::ToolActivity> none;
        return live_panel()->host() == nullptr ? none : live_panel()->host()->tools().activities();
    }

    std::vector<const Agent::ToolActivity*> live_calls(const std::string& tool) const
    {
        std::vector<const Agent::ToolActivity*> calls;
        for (const Agent::ToolActivity& activity : live_activities())
            if (activity.tool == tool)
                calls.push_back(&activity);
        return calls;
    }

    // Settled: nothing streaming, no tool mid-run, and either a card waits
    // for the person or the model has had its last word.
    bool live_settled() const
    {
        Agent::AgentHost* host = live_panel()->host();
        // A panel that closed has nothing more to say; the step's own
        // checks see what closed it.
        if (host == nullptr)
            return true;
        if (host->stream_active())
            return false;
        const auto& activities = live_activities();
        if (!activities.empty()) {
            const Agent::ToolState state = activities.back().state;
            if (state == Agent::ToolState::Approved || state == Agent::ToolState::Running)
                return false;
            if (state == Agent::ToolState::Pending)
                return true;
        }
        // A finished reply with no words is settled too; live_print_new
        // says so, rather than the run waiting on it forever.
        const auto messages = live_messages();
        return !messages.empty() && messages.back().role == Agent::MessageRole::Assistant &&
               messages.back().state == Agent::MessageState::Complete;
    }

    static const char* tool_state_label(Agent::ToolState state)
    {
        switch (state) {
        case Agent::ToolState::Pending: return "pending";
        case Agent::ToolState::Approved: return "approved";
        case Agent::ToolState::Running: return "running";
        case Agent::ToolState::Succeeded: return "succeeded";
        case Agent::ToolState::Failed: return "failed";
        case Agent::ToolState::Cancelled: return "cancelled";
        case Agent::ToolState::Rejected: return "rejected";
        }
        return "?";
    }

    void live_print_new()
    {
        if (live_panel()->host() == nullptr)
            std::cerr << "HARNESS LIVE   (the panel closed)\n";
        const auto messages = live_messages();
        for (std::size_t i = m_live_printed; i < messages.size(); ++i) {
            const auto& message = messages[i];
            const char* who = message.role == Agent::MessageRole::User ? "person" :
                              message.role == Agent::MessageRole::Note ? "app" :
                                                                         "model";
            if (!message.text.empty())
                std::cerr << "HARNESS LIVE   " << who << ": " << message.text << '\n';
            for (const nlohmann::json& receipt : live_receipts())
                if (receipt.value("afterMessageId", "") == message.id)
                    std::cerr << "HARNESS LIVE   receipt: Printer added, " << receipt["printer"].value("name", "") << ", "
                              << receipt["printer"].value("nozzle", 0.) << " mm nozzle\n";
        }
        if (m_live_printed < messages.size() && messages.back().role == Agent::MessageRole::Assistant && messages.back().text.empty())
            std::cerr << "HARNESS LIVE   model: (a finished reply with no words)\n";
        m_live_printed = messages.size();
    }

    // The "Printer added" cards the page draws in this session's thread.
    std::vector<nlohmann::json> live_receipts() const
    {
        std::vector<nlohmann::json> receipts;
        if (live_panel()->host() == nullptr)
            return receipts;
        for (const nlohmann::json& block : live_panel()->session_json().value("blocks", nlohmann::json::array()))
            if (block.value("kind", "") == "added")
                receipts.push_back(block);
        return receipts;
    }

    // One receipt per printer added, under a message the thread has.
    bool live_receipt_for(const std::string& printer) const
    {
        const auto receipts = live_receipts();
        if (receipts.size() != 1 || receipts.front()["printer"].value("name", "") != printer)
            return false;
        const auto messages = live_messages();
        return std::any_of(messages.begin(), messages.end(), [&](const Agent::ConversationMessage& message) {
            return message.id == receipts.front().value("afterMessageId", "");
        });
    }

    void run_live_steps()
    {
        if (m_live_steps.empty()) {
            finish_printer_live();
            return;
        }
        LiveStep step = std::move(m_live_steps.front());
        m_live_steps.pop_front();
        std::cerr << "HARNESS LIVE -- " << step.label << '\n';
        m_live_step = step.label;
        step.act();
        auto ready = step.ready ? step.ready : [this] { return live_settled(); };
        wait_until(
            ready, "live_" + step.label,
            [self = shared_from_this(), check = step.check] {
                self->live_print_new();
                if (check)
                    check();
                self->run_live_steps();
            },
            [this, ticks = std::make_shared<int>(0)] {
                if (++*ticks % 300 != 0 || live_panel()->host() == nullptr)
                    return;
                const auto messages = live_messages();
                std::cerr << "HARNESS LIVE   waiting: stream=" << live_panel()->host()->stream_active()
                          << " messages=" << messages.size() << " tools=" << live_activities().size();
                if (!messages.empty())
                    std::cerr << " last role=" << static_cast<int>(messages.back().role)
                              << " state=" << static_cast<int>(messages.back().state) << " text=" << messages.back().text.size()
                              << (messages.back().error ? " error=" + messages.back().error->code + " " + messages.back().error->message : "");
                std::cerr << '\n';
            });
    }

    void live_open(PrinterSetup::ConversationMode mode, const std::string& printer = {})
    {
        m_live_steps.push_back({mode == PrinterSetup::ConversationMode::Add ? "open: add" : "open: " + printer,
                                [this, mode, printer] {
                                    live_show(mode, printer);
                                    m_live_printed = 0;
                                },
                                [this] { return live_panel()->host() != nullptr && live_panel()->host()->handshake_complete() && live_panel()->instructions_ready(); },
                                {}});
    }

    void live_say(const std::string& words, std::function<void()> check)
    {
        m_live_steps.push_back({"say: " + words, [this, words] { say(words); }, {}, std::move(check)});
    }

    bool live_identified(const std::string& id) const
    {
        for (const Agent::ToolActivity* call : live_calls("printer_identify"))
            if (call->state == Agent::ToolState::Succeeded && call->arguments_json.find("\"" + id + "\"") != std::string::npos)
                return true;
        return false;
    }

    bool live_drew_a_card() const
    {
        for (const Agent::ToolActivity* call : live_calls("printer_identify"))
            if (call->state == Agent::ToolState::Succeeded)
                return true;
        return false;
    }

    void live_print_calls()
    {
        for (const Agent::ToolActivity& activity : live_activities())
            std::cerr << "HARNESS LIVE   tool " << activity.tool << ' ' << activity.arguments_json << " -> "
                      << (activity.error ? activity.error->code : std::string(tool_state_label(activity.state))) << '\n';
    }

    void begin_printer_live()
    {
        if (live_panel() == nullptr || std::getenv("OPENAI_API_KEY") == nullptr) {
            fail("the printer panel or the OpenAI key is missing");
            return;
        }
        // The panel builds its own Agent from the app configuration, as the
        // shipped app does, so the key goes there: this run's throwaway data
        // directory, never the person's.
        // As a std::string: a char* would pick AppConfig::set's bool overload.
        wxGetApp().app_config->set("jusprin_agent", "openai_api_key", std::string(std::getenv("OPENAI_API_KEY")));
        if (m_state->connect_capture) {
            queue_connect_capture();
            run_live_steps();
            return;
        }
        using Mode = PrinterSetup::ConversationMode;

        // Adding: nothing is saved until the person says yes.
        live_open(Mode::Add);
        live_say("the small bambu one", [this] {
            check_live_opening("Which printer do you have?", "live_add_opens_with_the_pages_line");
            live_print_calls();
            check(live_calls("printer_add").empty(), "live_nothing_added_before_the_person_says_which");
        });
        // Naming the model plainly adds it, with the nozzle it ships with.
        const auto added_printers = [this] {
            std::vector<std::string> names;
            for (const Agent::ToolActivity* call : live_calls("printer_add"))
                if (call->state == Agent::ToolState::Succeeded)
                    names.push_back(nlohmann::json::parse(call->result_json)["printer"].value("name", ""));
            return names;
        };
        live_say("the A1 mini", [this, added_printers] {
            live_print_calls();
            m_live_added = added_printers();
            check(m_live_added.size() == 1 && printer_profile(m_live_added.front()) != nullptr, "live_naming_the_model_adds_it");
            check(!m_live_added.empty() && live_receipt_for(m_live_added.front()), "live_an_added_printer_draws_its_receipt");
            live_capture("add-a1-mini");
        });
        live_say("yes, add it", [this, added_printers] {
            live_print_calls();
            check(added_printers().size() == 1, "live_a_second_yes_adds_no_second_printer");
            check(!m_live_added.empty() && live_receipt_for(m_live_added.front()), "live_a_second_yes_draws_no_second_receipt");
            live_capture("add-a1-mini-second-yes");
        });

        live_open(Mode::Add);
        live_say("prusa mk4", [this] {
            live_print_calls();
            // A name that begins several models is a question, not one of them.
            bool only_mk4s = true;
            std::size_t shown = 0;
            for (const Agent::ToolActivity* call : live_calls("printer_identify"))
                if (call->state == Agent::ToolState::Succeeded)
                    for (const auto& id : nlohmann::json::parse(call->arguments_json).value("catalogIds", nlohmann::json::array())) {
                        only_mk4s = only_mk4s && id.get<std::string>().rfind("Prusa/Prusa MK4", 0) == 0;
                        ++shown;
                    }
            check(only_mk4s && shown != 1, "live_prusa_mk4_asks_which");
            live_capture("add-prusa-mk4-asks-which");
        });
        // Answering with one of the three, as a tap on its choice sends it.
        live_say("Prusa MK4S", [this] {
            live_print_calls();
            std::string added;
            for (const Agent::ToolActivity* call : live_calls("printer_add"))
                if (call->state == Agent::ToolState::Succeeded)
                    added = nlohmann::json::parse(call->result_json)["printer"].value("name", "");
            if (!added.empty())
                m_live_added.push_back(added);
            check(!added.empty() && live_receipt_for(added), "live_a_chosen_printer_draws_its_receipt");
            live_capture("add-prusa-mk4s-chosen");
        });

        live_open(Mode::Add);
        live_say("my elegoo mars", [this] {
            live_print_calls();
            check(live_calls("printer_identify").empty() && live_calls("printer_add").empty(), "live_elegoo_mars_draws_no_card");
        });

        // The full list is OrcaSlicer's own window, opened from inside the
        // tool while the chat keeps pumping behind it. Closed here as a person
        // would close it; the tool must run once and the chat carry on.
        live_open(Mode::Add);
        m_live_steps.push_back({"say: show me the full printer list",
                                [this] {
                                    m_live_wizard_seen = false;
                                    say("show me the full printer list");
                                },
                                [this] {
                                    // Closed only once its page has loaded: the window installs
                                    // its page's script handler from a CallAfter that waits on the
                                    // page, and closing it first leaves that wait hanging on a
                                    // hidden view (see the old setup_manual check).
                                    for (wxWindow* window : wxTopLevelWindows)
                                        if (auto* guide = dynamic_cast<GuideFrame*>(window); guide != nullptr && guide->IsModal()) {
                                            if (wxGetApp().is_adding_script_handler())
                                                return false;
                                            std::function<wxWebView*(wxWindow*)> find = [&](wxWindow* parent) -> wxWebView* {
                                                if (auto* view = dynamic_cast<wxWebView*>(parent))
                                                    return view;
                                                for (wxWindow* child : parent->GetChildren())
                                                    if (wxWebView* view = find(child))
                                                        return view;
                                                return nullptr;
                                            };
                                            wxWebView* page = find(guide);
                                            if (page == nullptr || page->IsBusy() || page->GetCurrentURL().empty())
                                                return false;
                                            m_live_wizard_seen = true;
                                            guide->EndModal(wxID_CANCEL);
                                            return false;
                                        }
                                    return live_settled();
                                },
                                [this] {
                                    live_print_calls();
                                    const auto calls = live_calls("printer_manual_setup");
                                    check(m_live_wizard_seen, "live_full_list_opens_orcas_window");
                                    const auto messages = live_messages();
                                    check(calls.size() == 1 && calls.front()->state == Agent::ToolState::Succeeded &&
                                              nlohmann::json::parse(calls.front()->result_json).value("applied", true) == false,
                                          "live_full_list_runs_once_and_reports_nothing_added");
                                    check(!messages.empty() && messages.back().role == Agent::MessageRole::Assistant &&
                                              !messages.back().text.empty(),
                                          "live_the_chat_carries_on_after_the_window");
                                }});

        // Changing: a printer to change, the fixture's own profile under a
        // name, while the project keeps the fixture selected.
        m_live_steps.push_back({"make: Lab Printer",
                                [this] {
                                    // A named printer is saved from the selected system profile,
                                    // and the Add above left its own printer selected.
                                    SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
                                    m_live_printer = Printers::add_named_printer(*m_plater, "Lab Printer", {});
                                    SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
                                },
                                [] { return true; },
                                {}});
        m_live_steps.push_back({"open: change",
                                [this] {
                                    live_show(Mode::Change, m_live_printer);
                                    m_live_printed = 0;
                                },
                                [this] { return live_panel()->host() != nullptr && live_panel()->host()->handshake_complete() && live_panel()->instructions_ready(); },
                                {}});
        const auto parent_is = [this](const std::string& profile) {
            const Preset* printer = printer_profile(m_live_printer);
            return printer != nullptr && printer->inherits() == profile;
        };
        // A change the person states plainly is made at once; one the
        // assistant would have to guess is asked about first.
        const auto saved_changes = [this] {
            std::size_t saved = 0;
            for (const Agent::ToolActivity* call : live_calls("printer_change"))
                saved += call->state == Agent::ToolState::Succeeded ? 1 : 0;
            return saved;
        };
        live_say("I think I swapped the nozzle but I'm not sure which size", [this, parent_is, saved_changes] {
            check_live_opening("This is the " + m_live_printer + ". ", "live_change_opens_naming_the_printer");
            live_print_calls();
            check(saved_changes() == 0 && parent_is(kSetupFixturePrinter), "live_a_guess_is_asked_about_first");
        });
        live_say("i put a 0.6 nozzle on it", [this, parent_is] {
            live_print_calls();
            check(parent_is("Bambu Lab X1 Carbon 0.6 nozzle"), "live_a_plain_statement_changes_the_nozzle");
            check(selected_printer() == kSetupFixturePrinter, "live_nozzle_change_keeps_the_projects_printer");
        });
        live_say("actually put it back to 0.4", [this, parent_is] {
            live_print_calls();
            check(parent_is(kSetupFixturePrinter), "live_putting_it_back_is_the_same_tool");
        });
        live_say("swapped to a hardened steel 0.4", [this, parent_is, saved_changes] {
            check(saved_changes() == 2 && parent_is(kSetupFixturePrinter), "live_hardened_steel_changes_nothing");
        });
        live_say("the plate is smooth PEI now", [this, parent_is, saved_changes] {
            check(saved_changes() == 2 && parent_is(kSetupFixturePrinter), "live_plate_is_the_projects");
        });
        live_say("i put a 0.3 on it", [this, parent_is] {
            live_print_calls();
            check(parent_is(kSetupFixturePrinter), "live_0_3_is_never_saved");
        });

        // Connecting the printer added above, to the in-process fake Bambu
        // Lab printer: the code goes in on the card, never in the thread.
        static constexpr const char* kCode = "13572468";
        const auto card = [this]() -> const Agent::ToolActivity* {
            const auto calls = live_calls("printer_connect");
            return calls.empty() ? nullptr : calls.back();
        };
        // The fake is found the way a printer on the network is: discovery
        // starts, and the device list fills. Waited for here, so what the
        // model does next is about the conversation, not the network.
        m_live_steps.push_back({"fake printer on",
                                [this] {
                                    wxGetApp().app_config->set("jusprin", "fake_printer", "true");
                                    live_backend().prepare_connection(m_live_added.empty() ? std::string() : m_live_added.front());
                                },
                                [this] {
                                    return !m_live_added.empty() &&
                                           !live_backend().connection(m_live_added.front()).candidates.empty();
                                },
                                [this] {
                                    for (const auto& candidate : live_backend().connection(m_live_added.front()).candidates)
                                        std::cerr << "HARNESS LIVE   found " << candidate.id << ' ' << candidate.name
                                                  << (candidate.lan_mode ? " (LAN)" : " (account)") << '\n';
                                }});
        // The printer is the one the Add above saved, known only once it ran.
        m_live_steps.push_back({"open: connect the added printer",
                                [this] {
                                    live_show(Mode::Connect, m_live_added.empty() ? std::string() : m_live_added.front());
                                    m_live_printed = 0;
                                },
                                [this] { return live_panel()->host() != nullptr && live_panel()->host()->handshake_complete() && live_panel()->instructions_ready(); },
                                [this] { check(!m_live_added.empty(), "live_connect_has_the_added_printer"); }});
        // The model may open the card at once, or first ask whether this is
        // the printer: the script answers only if it asked.
        live_say("Yes, it is", [this] { live_print_calls(); });
        m_live_steps.push_back({"say, if still asked: Yes, connect it",
                                [this, card] {
                                    if (card() == nullptr || card()->state != Agent::ToolState::Pending)
                                        say("Yes, connect it");
                                },
                                {},
                                [this, card] {
                                    live_print_new();
                                    live_print_calls();
                                    check(card() != nullptr && card()->state == Agent::ToolState::Pending,
                                          "live_connect_asks_for_the_code_on_its_card");
                                }});
        live_say("where do I find the access code?", [this, card] {
            live_print_calls();
            check(card() != nullptr && card()->state == Agent::ToolState::Rejected, "live_writing_cancels_the_card");
            const auto messages = live_messages();
            check(!messages.empty() && messages.back().role == Agent::MessageRole::Assistant && !messages.back().text.empty(),
                  "live_the_question_is_answered");
        });
        live_say("ok, I have the code now. Connect it", [this, card] {
            live_print_calls();
            check(card() != nullptr && card()->state == Agent::ToolState::Pending, "live_connect_offers_a_fresh_card");
        });
        // Types the code on the card and taps Connect, then waits for the
        // app's note about how it went.
        const auto approve = [this, card](const std::string& label, const char* verified_check) {
            m_live_steps.push_back({label,
                                    [this, card] {
                                        if (card() != nullptr && card()->state == Agent::ToolState::Pending)
                                            live_send("tool_decision", {{"actionId", card()->action_id}, {"decision", "approve"},
                                                                        {"input", {{"credential", kCode}}}});
                                    },
                                    [this, card] {
                                        const auto calls = live_calls("printer_connect");
                                        const bool waiting = !calls.empty() && calls.back()->state == Agent::ToolState::Succeeded &&
                                                             calls.back()->result_json.find("connecting") != std::string::npos &&
                                                             !live_note_after(calls.back()->correlation_id);
                                        return !waiting && live_settled();
                                    },
                                    [this, verified_check] {
                                        live_print_calls();
                                        const auto messages = live_messages();
                                        check(std::any_of(messages.begin(), messages.end(), [](const auto& message) {
                                                  return message.role == Agent::MessageRole::Note && message.text.find("verified") != std::string::npos;
                                              }),
                                              verified_check);
                                    }});
        };
        approve("type the code, tap Connect", "live_connection_is_verified_and_reported");
        // A Moonraker printer, against the stand-in server the run was given
        // (PRINTER_LIVE_MOONRAKER): the API key goes in on the card and
        // must reach the printer's own request header.
        if (const char* host = std::getenv("PRINTER_LIVE_MOONRAKER")) {
            static constexpr const char* kKey = "moonkey123";
            const std::string address = host;
            m_live_steps.push_back({"make: a Klipper printer",
                                    [this] {
                                        const auto before = wxGetApp().app_config->vendors();
                                        std::string error;
                                        SetupCommands::install_and_select_printer(*m_plater, "Custom", "Generic Klipper Printer", "0.4", {}, error);
                                        Printers::name_installed_printers(*m_plater, before);
                                        m_live_host_printer = "Generic Klipper Printer";
                                        SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
                                    },
                                    [] { return true; },
                                    [this] { check(printer_profile(m_live_host_printer) != nullptr, "live_klipper_printer_saved"); }});
            m_live_steps.push_back({"open: connect the Klipper printer",
                                    [this] {
                                        live_show(Mode::Connect, m_live_host_printer);
                                        m_live_printed = 0;
                                    },
                                    [this] { return live_panel()->host() != nullptr && live_panel()->host()->handshake_complete() && live_panel()->instructions_ready(); },
                                    [this] { check_live_opening("Let’s connect " + m_live_host_printer + ".", "live_host_opening_asks_for_the_address"); }});
            live_say(address, [this] { live_print_calls(); });
            // It may ask which kind of server this is.
            m_live_steps.push_back({"say, if still asked: Moonraker",
                                    [this, card] {
                                        if (card() == nullptr || card()->state != Agent::ToolState::Pending)
                                            say("Moonraker");
                                    },
                                    {},
                                    [this, card] {
                                        live_print_new();
                                        live_print_calls();
                                        check(card() != nullptr && card()->state == Agent::ToolState::Pending &&
                                                  nlohmann::json::parse(card()->arguments_json).value("provider", "") == "host",
                                              "live_host_asks_for_the_key_on_its_card");
                                    }});
            // And asks while the printer is being checked: the question is
            // answered, and the app's note about the outcome comes after it.
            m_live_steps.push_back({"type the API key, tap Connect, ask how long it takes",
                                    [this, card] {
                                        if (card() != nullptr && card()->state == Agent::ToolState::Pending)
                                            live_send("tool_decision", {{"actionId", card()->action_id}, {"decision", "approve"},
                                                                        {"input", {{"credential", kKey}}}});
                                        say("how long does this take?");
                                    },
                                    [this, card] { return card() != nullptr && card()->state == Agent::ToolState::Succeeded &&
                                                          live_note_after(card()->correlation_id) && live_settled(); },
                                    [this] {
                                        live_print_new();
                                        live_print_calls();
                                        const auto messages = live_messages();
                                        check(std::any_of(messages.begin(), messages.end(), [](const auto& message) {
                                                  return message.role == Agent::MessageRole::Note &&
                                                         message.text.find("verified") != std::string::npos;
                                              }),
                                              "live_host_connection_is_verified");
                                        const char* log = std::getenv("PRINTER_LIVE_MOONRAKER_LOG");
                                        std::ifstream seen(log ? log : "");
                                        const std::string requests((std::istreambuf_iterator<char>(seen)), std::istreambuf_iterator<char>());
                                        check(requests.find("key=right") != std::string::npos, "live_the_typed_key_reaches_the_printer");
                                        bool leaked = false;
                                        for (const auto& message : live_messages())
                                            leaked = leaked || message.text.find(kKey) != std::string::npos;
                                        for (const auto& activity : live_activities())
                                            leaked = leaked || activity.arguments_json.find(kKey) != std::string::npos ||
                                                     activity.result_json.find(kKey) != std::string::npos;
                                        check(!leaked, "live_the_key_is_nowhere_in_the_conversation");
                                    }});
        }
        m_live_steps.push_back({"the code stays out of the conversation", [] {}, [] { return true; }, [this] {
                                    bool leaked = live_panel()->session_json().dump().find(kCode) != std::string::npos;
                                    for (const auto& message : live_messages())
                                        leaked = leaked || message.text.find(kCode) != std::string::npos;
                                    for (const auto& activity : live_activities())
                                        leaked = leaked || activity.arguments_json.find(kCode) != std::string::npos ||
                                                 activity.result_json.find(kCode) != std::string::npos;
                                    check(!leaked, "live_the_code_is_nowhere_in_the_conversation");
                                }});
        run_live_steps();
    }

    // --printer-connect-capture: connecting a Klipper printer with the live
    // model, against print hosts on loopback ports, pictured at each state of
    // the card: waiting, a question asked meanwhile, failed, cancelled,
    // connected, and Home afterwards.
    std::vector<std::shared_ptr<StandInHost>> m_capture_hosts;

    const Agent::ToolActivity* last_connect() const
    {
        const auto calls = live_calls("printer_connect");
        return calls.empty() ? nullptr : calls.back();
    }

    std::string connection_state(const std::string& action) const
    {
        return live_panel()->session_json()["connections"].value(action, nlohmann::json::object()).value("state", "");
    }

    void capture_web_view(wxWebView* view, const std::string& name)
    {
#ifdef __APPLE__
        fs::create_directories(m_state->capture_dir);
        const std::string file = (m_state->capture_dir / (name + ".png")).string();
        const bool ok = view != nullptr && snapshot_web_view(view->GetNativeBackend(), file.c_str());
        check(ok, "captured_" + name);
        if (ok)
            std::cout << "HARNESS ARTIFACT " << name << " " << file << std::endl;
#else
        fail("--printer-connect-capture pictures web views through WebKit, on macOS only");
#endif
    }

    void capture_panel(const std::string& name) { capture_web_view(live_panel()->web_view()->webview(), name); }

    // Gives `address` and taps Connect on the card the model draws for it,
    // naming the server if the model asks which kind it is.
    void connect_to(const std::string& words, const std::string& address, const std::string& picture)
    {
        m_live_steps.push_back({"say: " + words, [this, words] { say(words); }, {}, {}});
        m_live_steps.push_back({"say, if asked: Moonraker",
                                [this, address] {
                                    const auto* card = last_connect();
                                    if (card == nullptr || card->state != Agent::ToolState::Pending ||
                                        card->arguments_json.find(address) == std::string::npos)
                                        say("Moonraker");
                                },
                                {},
                                [this, address, picture] {
                                    live_print_calls();
                                    const auto* card = last_connect();
                                    check(card != nullptr && card->state == Agent::ToolState::Pending &&
                                              card->arguments_json.find(address) != std::string::npos,
                                          "capture_card_for_" + picture);
                                    if (!picture.empty())
                                        capture_panel(picture);
                                }});
        m_live_steps.push_back({"type the API key, tap Connect",
                                [this] {
                                    if (const auto* card = last_connect(); card != nullptr && card->state == Agent::ToolState::Pending)
                                        live_send("tool_decision", {{"actionId", card->action_id}, {"decision", "approve"},
                                                                    {"input", {{"credential", "moonkey123"}}}});
                                },
                                [this] {
                                    const auto* card = last_connect();
                                    return card != nullptr && card->state == Agent::ToolState::Succeeded && live_settled();
                                },
                                {}});
    }

    void queue_connect_capture()
    {
        using namespace std::chrono_literals;
        using Mode = PrinterSetup::ConversationMode;
        // Holds the request, then hangs up; holds it past the wait; answers.
        const auto failing   = std::make_shared<StandInHost>(12000ms, /*answer=*/false);
        const auto silent    = std::make_shared<StandInHost>(-1ms, /*answer=*/false);
        const auto answering = std::make_shared<StandInHost>(3000ms, /*answer=*/true);
        m_capture_hosts      = {failing, silent, answering};

        m_live_steps.push_back({"make: a Klipper printer",
                                [this] {
                                    const auto before = wxGetApp().app_config->vendors();
                                    std::string error;
                                    SetupCommands::install_and_select_printer(*m_plater, "Custom", "Generic Klipper Printer", "0.4", {}, error);
                                    Printers::name_installed_printers(*m_plater, before);
                                    m_live_host_printer = "Generic Klipper Printer";
                                    SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
                                },
                                [] { return true; },
                                [this] { check(printer_profile(m_live_host_printer) != nullptr, "capture_klipper_printer_saved"); }});
        m_live_steps.push_back({"open: connect the Klipper printer",
                                [this] {
                                    live_show(Mode::Connect, m_live_host_printer);
                                    m_live_printed = 0;
                                },
                                [this] { return live_panel()->host() != nullptr && live_panel()->host()->handshake_complete() && live_panel()->instructions_ready(); },
                                {}});

        // Waiting, and a question while it waits.
        connect_to(failing->address(), failing->address(), "1-card");
        m_live_steps.push_back({"picture: connecting", [] {}, {}, [this] {
                                    check(connection_state(last_connect()->action_id) == "connecting", "capture_is_connecting");
                                    capture_panel("2-connecting");
                                }});
        m_live_steps.push_back({"say: how long does this take?", [this] { say("how long does this take?"); }, {},
                                [this] {
                                    check(connection_state(last_connect()->action_id) == "connecting",
                                          "capture_answered_while_connecting");
                                    capture_panel("3-asked-while-waiting");
                                }});
        // It fails, and the model offers the ways forward.
        m_live_steps.push_back({"wait: the attempt fails", [] {},
                                [this] {
                                    const auto* card = last_connect();
                                    return connection_state(card->action_id) == "failed" && live_note_after(card->correlation_id) &&
                                           live_settled();
                                },
                                [this] { capture_panel("4-failed"); }});

        // Cancelled while it waits.
        connect_to("Let's try " + silent->address() + " instead.", silent->address(), "");
        m_live_steps.push_back({"tap Cancel on the waiting card",
                                [this] { live_send("printer_action", {{"action", "cancel_connection"}, {"actionId", last_connect()->action_id}}); },
                                // The app notes the cancel and starts no turn: nothing more is said.
                                [this] { return connection_state(last_connect()->action_id) == "cancelled"; },
                                [this] { capture_panel("5-cancelled"); }});

        // Connected.
        connect_to("Try " + answering->address() + ".", answering->address(), "");
        m_live_steps.push_back({"wait: the attempt is verified", [] {},
                                [this] {
                                    const auto* card = last_connect();
                                    return connection_state(card->action_id) == "verified" && live_note_after(card->correlation_id) &&
                                           live_settled();
                                },
                                [this] {
                                    check(printer_profile(m_live_host_printer)->config.opt_string("print_host") ==
                                              m_capture_hosts.back()->address(),
                                          "capture_verified_address_saved");
                                    capture_panel("6-connected");
                                }});
        // Home, with the printer the panel connected.
        m_live_steps.push_back({"back to Home", [this] { live_panel()->close(); },
                                [this] { return !live_panel()->IsShown() && installed_shell()->home_view()->IsShown(); },
                                [this] {
                                    for (int settle = 0; settle < 20; ++settle) {
                                        wxYield();
                                        wxMilliSleep(50);
                                    }
                                    capture_web_view(installed_shell()->home_view()->webview(), "7-home");
                                }});
    }

    void finish_printer_live()
    {
        live_panel()->close();
        wxYield();
        if (!m_live_printer.empty())
            Printers::remove_named_printer(*m_plater, nullptr, m_live_printer);
        for (const std::string& added : m_live_added)
            Printers::remove_named_printer(*m_plater, nullptr, added);
        if (!m_live_host_printer.empty())
            Printers::remove_named_printer(*m_plater, nullptr, m_live_host_printer);
        SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
        finish();
    }

    void verify_setup_install_commands()
    {
        const std::string before = selected_printer();
        const auto vendors_before = wxGetApp().app_config->vendors();
        std::string error;
        const bool missing_variant = SetupCommands::install_and_select_printer(
            *m_plater, "Creality", "Creality Ender-3 V2", "9.9", {}, error);
        check(!missing_variant && !error.empty(), "setup_install_failure_reported");
        check(selected_printer() == before, "setup_install_failure_keeps_previous_printer");
        check(wxGetApp().app_config->vendors() == vendors_before, "setup_install_failure_restores_enabled_printers");

        error.clear();
        const bool enabled = SetupCommands::install_and_select_printer(
            *m_plater, "BBL", "Bambu Lab X1 Carbon", "0.4", {}, error);
        check(enabled && error.empty() && selected_printer() == kSetupFixturePrinter,
              "setup_install_selects_an_already_enabled_profile");
        verify_named_printers();
    }

    static Preset* printer_profile(const std::string& name)
    {
        return wxGetApp().preset_bundle->printers.find_preset(name, false, true);
    }

    struct HostOutcome
    {
        std::string               state;
        int                       ticks{0};
        std::chrono::milliseconds took{0};
    };

    // Connects `name` to a Moonraker host and waits up to `limit` for the
    // outcome, counting a 100 ms timer's ticks meanwhile: a test run on the
    // main thread stops the app, and the timer with it.
    static HostOutcome connect_host_and_wait(PrinterSetup::OrcaPrinterBackend& backend, const std::string& name,
                                             const std::string& address, std::chrono::seconds limit)
    {
        HostOutcome outcome;
        wxTimer     timer;
        timer.Bind(wxEVT_TIMER, [&outcome](wxTimerEvent&) { ++outcome.ticks; });
        timer.Start(100);
        const auto        start   = std::chrono::steady_clock::now();
        const std::string problem = backend.connect_host(name, "moonraker", address, "");
        outcome.state             = problem.empty() ? backend.connection(name).state : "refused: " + problem;
        while (outcome.state == "connecting" && std::chrono::steady_clock::now() - start < limit) {
            wxYield();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            outcome.state = backend.connection(name).state;
        }
        timer.Stop();
        outcome.took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        std::cout << "HARNESS host connect to " << address << ": " << outcome.state << " after " << outcome.took.count()
                  << " ms, " << outcome.ticks << " timer ticks" << std::endl;
        return outcome;
    }

    // Waits, yielding to the app, until `done` or `limit` passes.
    static bool wait_for(const std::function<bool()>& done, std::chrono::milliseconds limit)
    {
        const auto until = std::chrono::steady_clock::now() + limit;
        while (!done()) {
            if (std::chrono::steady_clock::now() > until)
                return false;
            wxYield();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return true;
    }

    // Testing a print host takes as long as the host takes to answer, and
    // the app goes on meanwhile: a host that holds the request for three
    // seconds leaves a 100 ms timer ticking the whole time. The address is
    // saved only once the host has answered, and an answer nobody waits for
    // any more is dropped.
    void verify_host_connection_keeps_the_app_responsive(PrinterSetup::OrcaPrinterBackend& backend)
    {
        using namespace std::chrono_literals;
        const auto saved_address = [] { return printer_profile(kAddedPrinter)->config.opt_string("print_host"); };
        const std::string address_before = saved_address();
        {
            StandInHost       slow(3000ms, /*answer=*/false);
            const HostOutcome outcome = connect_host_and_wait(backend, kAddedPrinter, slow.address(), 20s);
            check(outcome.state == "failed" && outcome.took >= 2500ms, "host_slow_failure_is_reported");
            check(outcome.ticks >= 10, "host_test_leaves_the_app_responsive");
            check(saved_address() == address_before, "host_failure_leaves_the_profile_as_it_was");
        }
        std::string answered;
        {
            StandInHost       host(0ms, /*answer=*/true);
            const HostOutcome outcome = connect_host_and_wait(backend, kAddedPrinter, host.address(), 20s);
            answered = host.address();
            check(outcome.state == "verified" && host.requests() == 1, "host_that_answers_is_verified");
            check(saved_address() == answered &&
                      printer_profile(kAddedPrinter)->config.opt_enum<PrintHostType>("host_type") == htMoonraker,
                  "host_success_saves_the_address");
        }
        {
            // Would answer in two seconds; cancelled first.
            StandInHost late(2000ms, /*answer=*/true);
            check(backend.connect_host(kAddedPrinter, "moonraker", late.address(), "").empty() &&
                      backend.connection(kAddedPrinter).state == "connecting",
                  "host_attempt_starts_waiting");
            backend.cancel_connection(kAddedPrinter);
            wait_for([] { return false; }, 3000ms);
            check(backend.connection(kAddedPrinter).state != "verified" && saved_address() == answered,
                  "host_cancel_drops_a_late_answer");
        }
        {
            // Replaced by a second attempt, to a closed port, before it answers.
            StandInHost late(2000ms, /*answer=*/true);
            check(backend.connect_host(kAddedPrinter, "moonraker", late.address(), "").empty(), "host_first_attempt_starts");
            const HostOutcome second = connect_host_and_wait(backend, kAddedPrinter, "http://127.0.0.1:1", 20s);
            wait_for([] { return false; }, 3000ms);
            check(second.state == "failed" && backend.connection(kAddedPrinter).state == "failed" && saved_address() == answered,
                  "host_replaced_attempt_answer_is_dropped");
        }
        {
            // Accepts and never answers: failed at the wait, not never.
            StandInHost       silent(-1ms, /*answer=*/false);
            const HostOutcome outcome = connect_host_and_wait(backend, kAddedPrinter, silent.address(), 45s);
            check(outcome.state == "failed" && outcome.took >= PrinterSetup::kConnectionWait &&
                      outcome.took < PrinterSetup::kConnectionWait + 5s,
                  "host_that_never_answers_fails_at_the_wait");
            check(backend.connection(kAddedPrinter).message.find("did not respond") != std::string::npos,
                  "host_silence_is_reported_as_no_response");
            check(saved_address() == answered, "host_silence_leaves_the_profile");
        }
        verify_panel_close_drops_a_waiting_connection(answered);
    }

    // Opens the panel on `printer` with a model that answers the first
    // message by calling `tool`, sends that message, and returns the call.
    const Agent::ToolActivity* call_in_panel(const std::string& printer, const std::string& tool, const nlohmann::json& arguments,
                                             const std::string& name)
    {
        using namespace std::chrono_literals;
        PrinterSetup::PrinterPanel* panel = installed_shell()->printer_panel();
        panel->open(PrinterSetup::ConversationMode::Connect, printer);
        const bool loaded = wait_for([panel] {
            return panel->host() != nullptr && panel->host()->handshake_complete() && panel->instructions_ready();
        }, 60s);
        check(loaded, name + "_page_loaded");
        if (!loaded)
            return nullptr;
        Agent::AgentHost* host = panel->host();
        host->set_agent(std::make_unique<ToolCallingAgent>(tool, arguments), Agent::AgentAvailability::Ready);
        static int next = 0;
        host->on_page_message(nlohmann::json{{"protocol", Agent::Protocol::kName},
                                             {"version", Agent::Protocol::kVersion},
                                             {"id", "call-" + std::to_string(++next)},
                                             {"type", "user_message"},
                                             {"payload", {{"clientMessageId", "call-c-" + std::to_string(next)}, {"text", "go on"}}}}
                                  .dump());
        const auto call = [host, tool]() -> const Agent::ToolActivity* {
            for (const auto& activity : host->tools().activities())
                if (activity.tool == tool)
                    return &activity;
            return nullptr;
        };
        const bool made = wait_for([&] {
            const auto* activity = call();
            return activity != nullptr && activity->state != Agent::ToolState::Approved && activity->state != Agent::ToolState::Running;
        }, 10s);
        check(made, name + "_tool_called");
        return made ? call() : nullptr;
    }

    // The panel closed while its printer was still being checked, and the
    // host answers afterwards: nothing is saved, then or when the panel next
    // looks at the connection.
    void verify_panel_close_drops_a_waiting_connection(const std::string& saved)
    {
        using namespace std::chrono_literals;
        PrinterSetup::PrinterPanel* panel = installed_shell()->printer_panel();
        StandInHost                 late(2000ms, /*answer=*/true);
        const Agent::ToolActivity*  card = call_in_panel(kAddedPrinter, "printer_connect",
                                                         nlohmann::json{{"hostType", "moonraker"}, {"address", late.address()}},
                                                         "panel_close_fixture");
        check(card != nullptr && card->state == Agent::ToolState::Pending, "panel_close_fixture_card_drawn");
        if (card == nullptr)
            return;
        const std::string action = card->action_id;
        panel->host()->on_page_message(nlohmann::json{{"protocol", Agent::Protocol::kName},
                                                      {"version", Agent::Protocol::kVersion},
                                                      {"id", "close-decision"},
                                                      {"type", "tool_decision"},
                                                      {"payload", {{"actionId", action}, {"decision", "approve"}, {"input", {{"credential", ""}}}}}}
                                           .dump());
        check(wait_for([&] {
                  return panel->session_json()["connections"].value(action, nlohmann::json::object()).value("state", "") == "connecting";
              }, 10s),
              "panel_close_fixture_is_connecting");
        panel->close();
        wait_for([] { return false; }, 3000ms);
        check(!panel->IsShown() && late.requests() == 1, "panel_close_mid_test_closes");

        // Opened again: the status the model reads does not bring the old
        // answer back.
        const Agent::ToolActivity* status = call_in_panel(kAddedPrinter, "printer_connection_status", nlohmann::json::object(),
                                                          "panel_reopen");
        check(status != nullptr && status->state == Agent::ToolState::Succeeded &&
                  nlohmann::json::parse(status->result_json).value("state", "") != "verified",
              "panel_reopen_does_not_read_the_old_answer");
        check(printer_profile(kAddedPrinter)->config.opt_string("print_host") == saved, "panel_close_mid_test_saves_nothing");
        panel->close();
        wait_for([panel] { return !panel->IsShown(); }, 5s);
    }

    // A printer is a named user profile: a second one of a model is a second
    // printer, Home lists each, and renaming or removing one leaves the rest.
    void verify_named_printers()
    {
        const auto catalog = PrinterSetup::PrinterCatalog::load(Slic3r::resources_dir());
        const auto neo = std::find_if(catalog.candidates().begin(), catalog.candidates().end(), [](const auto& candidate) {
            return candidate.model_id == "Creality Ender-3 V2 Neo" && candidate.variant == "0.4";
        });
        check(neo != catalog.candidates().end(), "named_catalog_has_the_neo");
        if (neo == catalog.candidates().end()) {
            fail("the catalogue has no Ender-3 V2 Neo 0.4");
            return;
        }

        // What "Add this printer" does, through the one route the panel uses.
        PrinterSetup::OrcaPrinterBackend backend(*m_plater, PrinterSetup::PrinterCatalog::load(Slic3r::resources_dir()),
                                                 nullptr);
        PrinterSetup::AddPrinterRequest request;
        request.vendor_id = neo->vendor_id;
        request.model_id  = neo->model_id;
        request.variant   = neo->variant;
        request.material  = neo->default_material;
        request.name      = neo->model_name;
        PrinterSetup::SavedPrinter saved;
        std::string error = backend.add_printer(request, saved);

        const Preset* first = printer_profile(kAddedPrinter);
        check(error.empty() && first != nullptr && first->is_user() && first->inherits() == kAddedPrinterProfile,
              "named_add_saved_a_user_profile_on_the_system_one");

        // A second printer of the same model is a second printer.
        error = backend.add_printer(request, saved);
        const bool added = error.empty();
        const std::string second = std::string(kAddedPrinter) + " (2)";
        check(added && error.empty() && selected_printer() == second, "named_second_of_a_model_is_a_second_printer");
        check(printer_profile(kAddedPrinter) != nullptr, "named_second_add_keeps_the_first");

        Home::OrcaHomeBackend home(*m_frame, nullptr);
        const auto cards = home.printers();
        const auto card = [&](const std::string& name) -> const Home::PrinterEntry* {
            const auto found = std::find_if(cards.begin(), cards.end(),
                                            [&](const Home::PrinterEntry& entry) { return entry.id == "named:" + name; });
            return found == cards.end() ? nullptr : &*found;
        };
        const auto* listed = card(second);
        check(card(kAddedPrinter) != nullptr && listed != nullptr && listed->name == second,
              "named_home_lists_both_printers_by_name");
        check(listed != nullptr && listed->kind == Home::PrinterKind::Named && listed->can_open_settings &&
                  listed->can_rename && listed->can_remove,
              "named_home_offers_the_printer_menu");

        verify_host_connection_keeps_the_app_responsive(backend);

        // Configure an unselected saved printer through the real adapter. A
        // closed loopback port exercises an actual provider failure without
        // contacting hardware or sending any file.
        const bool        dirty_before_connection = m_plater->is_project_dirty();
        const std::string address_before          = printer_profile(kAddedPrinter)->config.opt_string("print_host");
        check(connect_host_and_wait(backend, kAddedPrinter, "http://127.0.0.1:1", std::chrono::seconds(20)).state == "failed",
              "named_host_test_failure_is_a_connection_outcome");
        check(printer_profile(kAddedPrinter) != nullptr && selected_printer() == second &&
                  m_plater->is_project_dirty() == dirty_before_connection,
              "named_connect_preserves_saved_printer_and_unrelated_project");
        check(printer_profile(kAddedPrinter)->config.opt_string("print_host") == address_before,
              "named_host_failure_keeps_the_saved_address");
        auto& printer_presets = wxGetApp().preset_bundle->printers;
        const bool dirty_on_selected = m_plater->is_project_dirty();
        check(Printers::configure_named_printer_host(second, htOctoPrint, "http://127.0.0.1:1", "").empty() &&
                  printer_presets.get_edited_preset().config.opt_string("print_host") == "http://127.0.0.1:1" &&
                  !printer_presets.current_is_dirty() && m_plater->is_project_dirty() == dirty_on_selected,
              "named_host_settings_update_selected_printer_without_changing_project_edits");
        printer_presets.get_edited_preset().config.set_key_value("printer_notes", new ConfigOptionString("unsaved"));
        check(!Printers::configure_named_printer_host(second, htMoonraker, "http://127.0.0.1:2", "").empty() &&
                  printer_presets.get_edited_preset().config.opt_string("printer_notes") == "unsaved" &&
                  printer_profile(second)->config.opt_string("print_host") == "http://127.0.0.1:1",
              "named_connect_refuses_to_overwrite_unsaved_printer_edits");
        printer_presets.discard_current_changes();
        check(Printers::link_named_printer(kAddedPrinter, "harness-device").empty() &&
                  !Printers::link_named_printer(second, "harness-device").empty(),
              "named_device_link_refuses_a_second_owner");

        check(!Printers::rename_named_printer(*m_plater, nullptr, second, kAddedPrinter).empty() &&
                  selected_printer() == second,
              "named_rename_refuses_a_taken_name");
        check(!Printers::rename_named_printer(*m_plater, nullptr, second, "Shed/Neo").empty(),
              "named_rename_refuses_illegal_characters");
        check(Printers::rename_named_printer(*m_plater, nullptr, second, "Shed Neo").empty() &&
                  selected_printer() == "Shed Neo" && printer_profile(second) == nullptr,
              "named_rename_of_the_selected_printer");
        check(Printers::rename_named_printer(*m_plater, nullptr, kAddedPrinter, "Garage Neo").empty() &&
                  selected_printer() == "Shed Neo" && printer_profile(kAddedPrinter) == nullptr,
              "named_rename_of_another_printer_keeps_the_selection");
        const Preset* garage = printer_profile("Garage Neo");
        check(garage != nullptr && garage->inherits() == kAddedPrinterProfile, "named_rename_keeps_the_parent");
        const auto renamed_printers = Printers::named_printers();
        check(std::any_of(renamed_printers.begin(), renamed_printers.end(), [](const auto& printer) {
                  return printer.name == "Garage Neo" && printer.device_id == "harness-device";
              }), "named_rename_keeps_the_device_association");
        check(Printers::remove_named_printer(*m_plater, nullptr, "Garage Neo").empty() &&
                  printer_profile("Garage Neo") == nullptr && selected_printer() == "Shed Neo",
              "named_remove_of_another_printer_keeps_the_selection");
        check(Printers::remove_named_printer(*m_plater, nullptr, "Shed Neo").empty() &&
                  printer_profile("Shed Neo") == nullptr && selected_printer() == kAddedPrinterProfile,
              "named_remove_of_the_selected_printer_selects_its_parent");
        verify_named_header();
    }

    // The header names the selected printer by its own name.
    void verify_named_header()
    {
        const std::string name = Printers::add_named_printer(*m_plater, "Lab Printer", {});
        check(SetupCommands::current_printer().nickname == name, "named_header_shows_the_printers_name");
        check(Printers::remove_named_printer(*m_plater, nullptr, name).empty(), "named_header_cleanup_removes_the_printer");
        verify_named_wizard_install();
    }

    // "Set it up myself" runs Orca's wizard, which only enables models. What
    // it newly enabled becomes a printer, as a recognized model does; the
    // wizard itself is a web page this harness cannot drive, so the enabling
    // is done the way the wizard's apply does it.
    void verify_named_wizard_install()
    {
        const auto before  = wxGetApp().app_config->vendors();
        const auto custom  = before.find("Custom");
        check(custom == before.end() || custom->second.count("Generic Klipper Printer") == 0 ||
                  custom->second.at("Generic Klipper Printer").empty(),
              "named_wizard_klipper_not_enabled_before");
        std::string error;
        check(SetupCommands::install_and_select_printer(*m_plater, "Custom", "Generic Klipper Printer", "0.4", {}, error),
              "named_wizard_enables_klipper");
        Printers::name_installed_printers(*m_plater, before);
        const Preset* klipper = printer_profile("Generic Klipper Printer");
        check(selected_printer() == "Generic Klipper Printer" && klipper != nullptr && klipper->is_user() &&
                  klipper->inherits() == "MyKlipper 0.4 nozzle",
              "named_wizard_install_becomes_a_printer");
        Printers::name_installed_printers(*m_plater, wxGetApp().app_config->vendors());
        check(printer_profile("Generic Klipper Printer (2)") == nullptr, "named_wizard_names_only_new_models");
        check(Printers::remove_named_printer(*m_plater, nullptr, "Generic Klipper Printer").empty(),
              "named_wizard_cleanup_removes_the_printer");
        SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
        check(selected_printer() == kSetupFixturePrinter, "named_cleanup_restores_the_fixture_printer");
        verify_saved_bambu_connection();
    }

    void verify_saved_bambu_connection()
    {
        // Exercise the actual adapter and parsed device observations with the
        // in-process fake. This proves state ownership, not network authentication.
        wxGetApp().app_config->set("jusprin", "fake_printer", "true");
        auto backend = std::make_shared<PrinterSetup::OrcaPrinterBackend>(
            *m_plater, PrinterSetup::PrinterCatalog::load(Slic3r::resources_dir()), nullptr);
        PrinterSetup::AddPrinterRequest request;
        request.vendor_id = "BBL";
        request.model_id = "Bambu Lab A1 mini";
        request.variant = "0.6";
        request.name = "Connection fixture";
        PrinterSetup::SavedPrinter saved;
        check(backend->add_printer(request, saved).empty(), "connect_fixture_saved_without_device");
        SetupCommands::select_printer_preset(*m_plater, kAddedPrinterProfile);
        check(selected_printer() == kAddedPrinterProfile && !wxGetApp().preset_bundle->is_bbl_vendor(),
            "connection_test_uses_a_non_bambu_slicing_profile");
        const bool dirty = m_plater->is_project_dirty();
        backend->prepare_connection(saved.name);
        wait_until([backend, name = saved.name] { return !backend->connection(name).candidates.empty(); },
            "connect_fake_discovered_by_real_adapter", [self = shared_from_this(), backend, name = saved.name, dirty] {
                const auto info = backend->connection(name);
                const std::string device = info.candidates.front().id;
                self->check(info.state == "not_configured", "unlinked_online_device_is_not_a_connected_saved_printer");
                wxGetApp().getDeviceManager()->get_my_machine(device)->reset(); // Ignore the fake's unsolicited heartbeat.
                self->check(Printers::link_named_printer(name, device).empty(), "discovered_device_linked_before_authentication");
                self->check(backend->connection(name).state == "not_configured", "identified_device_has_neutral_first_connection_state");
                self->check(backend->connect_printer(name, device, "").empty(), "connect_existing_saved_bambu");
                self->check(backend->connection(name).state == "connecting", "connect_waits_for_fresh_device_data");
                self->check(backend->connect_printer(name, device, "").empty(), "duplicate_connect_is_idempotent");
                // The person picks another printer later, not in the same event
                // turn: a connect re-selects an already-selected printer on the next.
                wxYield();
                wxGetApp().getDeviceManager()->set_selected_machine("");
                const auto interrupted = backend->connection(name);
                self->check(interrupted.state == "failed" && interrupted.message.find("The selected printer changed") != std::string::npos &&
                    interrupted.message.find("access code") == std::string::npos, "selection_interruption_does_not_blame_credentials");
                // Separate gestures by a GUI event turn, allowing upstream disconnect
                // notifications to finish before the next user-requested connection.
                wxGetApp().CallAfter([self, backend, name, device, dirty] {
                self->check(backend->connect_printer(name, device, "").empty(), "interrupted_connection_can_retry");
                self->wait_until([backend, name] { return backend->connection(name).state == "verified"; },
                    "connect_verified_from_parsed_fake_data", [self, backend, name, device, dirty] {
                        self->check(backend->connection(name).nozzle_mismatch, "reported_nozzle_mismatch_does_not_block_connection");
                        self->check(wxGetApp().app_config->get("jusprin_verified_connections", device) == "true",
                            "verified_connection_history_is_recorded_for_this_device");
                        const auto printers = backend->saved_printers();
                        self->check(std::count_if(printers.begin(), printers.end(), [&](const auto& p) {
                            return p.name == name && p.device_id == device;
                        }) == 1, "connect_keeps_one_saved_identity");
                        self->check(self->selected_printer() == kAddedPrinterProfile &&
                            self->m_plater->is_project_dirty() == dirty, "connect_preserves_unrelated_project_selection_and_edits");
                        // Profile changes go through Orca's own preset selection, which
                        // re-evaluates the printer agent. The printer's own Bambu profile
                        // keeps the connection; a non-Bambu profile hands the agent back
                        // to Orca's profile-driven choice, as stock Orca does.
                        const auto agent_id = [] { return wxGetApp().getAgent()->get_printer_agent()->get_agent_info().id; };
                        const std::string fake = fake_bambu_printer_agent_id(wxGetApp().app_config);
                        self->check(SetupCommands::select_printer_preset(*self->m_plater, name) && agent_id() == fake &&
                            backend->connection(name).state == "verified", "selecting_the_printers_own_profile_keeps_the_connection");
                        self->check(SetupCommands::select_printer_preset(*self->m_plater, kAddedPrinterProfile) && agent_id() != fake,
                            "selecting_a_non_bambu_profile_replaces_connection_provider");
                        const auto replaced = std::chrono::steady_clock::now();
                        std::cerr << "HARNESS NOTE state_after_provider_replaced " << backend->connection(name).state << '\n';
                        self->wait_until([backend, name] { return backend->connection(name).state == "unknown"; },
                            "home_reports_unknown_after_provider_replaced", [self, backend, name, device, replaced, agent_id, fake] {
                        std::cerr << "HARNESS NOTE seconds_until_unknown " << std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - replaced).count() << '\n';
                        backend->prepare_connection(name);
                        self->check(agent_id() == fake, "connecting_again_reinstalls_provider");
                        wxGetApp().CallAfter([self, backend, name, device] {
                        self->check(backend->connect_printer(name, device, "").empty(), "reconnect_after_profile_reevaluation");
                        self->wait_until([backend, name] { return backend->connection(name).state == "verified"; },
                            "connection_receives_fresh_data_after_reconnect", [self, backend, name, device] {
                                wxGetApp().getDeviceManager()->set_selected_machine("");
                                wxGetApp().CallAfter([self, backend, name, device] {
                                // Simulate a transport that authenticates but never supplies status.
                                // The isolated fake otherwise pushes even while disconnected.
                                auto fake = wxGetApp().getAgent()->get_printer_agent();
                                fake->set_on_local_message_fn({});
                                fake->set_on_message_fn({});
                                self->check(backend->connect_printer(name, device, "").empty(), "timeout_fixture_starts_attempt");
                                self->wait_until([backend, name] { return backend->connection(name).state == "failed"; },
                                    "connection_times_out_without_messages", [self, backend, name] {
                                        const auto timeout = backend->connection(name);
                                        self->check(timeout.message.find("did not respond") != std::string::npos &&
                                            timeout.message.find("access code") == std::string::npos,
                                            "timeout_explains_no_response_without_inventing_auth_failure");
                                        PrinterSetup::OrcaPrinterBackend reopened(*self->m_plater,
                                            PrinterSetup::PrinterCatalog::load(Slic3r::resources_dir()), nullptr);
                                        self->check(reopened.connection(name).state == "unknown",
                                            "previously_verified_connection_remains_distinct_from_never_connected");
                                        self->finish();
                                    });
                                });
                            });
                        });
                        });
                    });
                });
            });
    }

    void check(bool condition, const std::string& name)
    {
        std::cerr << "HARNESS CHECK " << name << ' ' << (condition ? "PASS" : "FAIL") << '\n';
        if (!condition)
            ++m_failures;
    }

    // Every label rendered by the status row, joined. Read by walking the
    // widget tree so the row needs no test-only accessor.
    static wxString status_row_labels(wxWindow* window)
    {
        wxString text = window->GetLabel() + "\n";
        for (wxWindow* child : window->GetChildren())
            text += status_row_labels(child);
        return text;
    }

    void verify_stock_mode()
    {
        check(installed_shell() == nullptr, "stock_mode_installs_no_shell");
        check(m_notebook->GetBtnsListCtrl()->IsShown(), "stock_mode_keeps_tab_strip");
        check(m_plater->is_sidebar_available(), "stock_mode_keeps_sidebar_available");
        load_multi_plate_fixture();
        check(m_plater->canvas3D()->get_volumes_count() >= 2, "stock_mode_canvas_renders_fixture");
        // Exiting with loaded volumes trips an inherited teardown crash in
        // ~GLCanvas3D (Selection::clear() re-enters the destroyed Plater);
        // see the Phase 1 handback. End on an empty project like the shell
        // scenario does.
        check(m_plater->new_project(true, true) != wxID_CANCEL, "stock_mode_teardown_project");
    }

    void verify_shell_installed()
    {
        ShellController* shell = installed_shell();
        check(shell != nullptr && shell->is_installed(), "shell_installed");
        if (shell == nullptr)
            throw std::runtime_error("shell is not installed");
        // Startup lands on Home, which shows no header and collapses the Agent
        // pane on the person's behalf. verify_agent_pane_follows_page checks
        // both return on Prepare, and checks the header's layout there.
        check(m_notebook->GetSelection() == MainFrame::tpHome, "shell_starts_on_home");
        check(shell->status_row() != nullptr && !shell->status_row()->IsShown(), "status_row_hidden_on_home");
        check(shell->status_row()->project_summary().Contains(wxString::FromUTF8("Prints \xC2\xB7 0")),
              "overflow_summary_shows_empty_print_count");
        verify_type_roles_render_at_token_size();
        check(shell->agent_pane() != nullptr && shell->is_agent_pane_collapsed() && !shell->agent_pane()->IsShown(),
              "agent_pane_collapsed_on_home");
        check(shell->agent_pane()->web_view().host().mcp() != nullptr, "shell_starts_mcp_automatically");
        check(!m_notebook->GetBtnsListCtrl()->IsShown(), "tab_strip_hidden");
        check(!m_plater->is_sidebar_available(), "sidebar_marked_unavailable");
        // The shell hides the entire legacy canvas-overlay layer on Prepare.
        check(m_plater->get_view3D_canvas3D()->legacy_overlays_hidden(), "prepare_legacy_overlays_hidden");

        // Later Orca paths call enable_sidebar(true); the persistent policy
        // must keep the sidebar down until the shell releases it.
        m_plater->enable_sidebar(true);
        check(!wxGetApp().sidebar().IsShown(), "sidebar_stays_hidden_after_reenable_attempt");

        // A second installation attempt must fail without disturbing layout.
        ShellController second;
        bool second_install_failed = false;
        try {
            second.install(*m_frame, *m_notebook, *frame_main_sizer());
        } catch (const std::exception&) {
            second_install_failed = true;
        }
        check(second_install_failed, "second_install_rejected");
        check(installed_shell()->is_installed() && installed_shell()->status_row()->GetContainingSizer() != nullptr,
              "shell_survives_rejected_install");
    }

    struct PaneStep
    {
        int                   tab;
        std::string           name;
        std::function<void()> on_arrival;
    };

    // Home shows no header and always collapses the Agent pane; leaving Home
    // shows the header and restores what the person last chose for the pane
    // on Prepare, in both directions. Ends back on Home, where every mode
    // started before this check existed.
    void verify_agent_pane_follows_page(std::function<void()> next)
    {
        ShellController* shell = installed_shell();
        auto collapsed = [shell] { return shell->is_agent_pane_collapsed() && !shell->agent_pane()->IsShown(); };
        auto expanded  = [shell] { return !shell->is_agent_pane_collapsed() && shell->agent_pane()->IsShown(); };
        auto steps = std::make_shared<std::vector<PaneStep>>(std::vector<PaneStep>{
            {MainFrame::tp3DEditor, "agent_pane_prepare_first_visit", [this, shell, collapsed, expanded] {
                 check(shell->status_row()->IsShown(), "status_row_shown_on_prepare");
                 verify_header_layout();
                 check(expanded(), "agent_pane_shown_on_prepare");
                 shell->toggle_agent_pane();
                 check(collapsed(), "agent_pane_user_collapses_on_prepare");
             }},
            {MainFrame::tpHome, "agent_pane_home_after_user_collapse", [this, shell, collapsed] {
                 check(!shell->status_row()->IsShown(), "status_row_hidden_on_return_to_home");
                 check(collapsed(), "agent_pane_collapsed_on_home_after_user_collapse");
             }},
            {MainFrame::tp3DEditor, "agent_pane_prepare_after_user_collapse", [this, shell, collapsed, expanded] {
                 check(collapsed(), "agent_pane_user_collapse_restored_on_prepare");
                 shell->toggle_agent_pane();
                 check(expanded(), "agent_pane_user_expands_on_prepare");
             }},
            {MainFrame::tpHome, "agent_pane_home_after_user_expand",
             [this, collapsed] { check(collapsed(), "agent_pane_collapsed_on_home_after_user_expand"); }},
            {MainFrame::tp3DEditor, "agent_pane_prepare_after_user_expand",
             [this, expanded] { check(expanded(), "agent_pane_user_expand_restored_on_prepare"); }},
            {MainFrame::tpHome, "agent_pane_returns_home", [] {}},
        });
        run_pane_steps(steps, 0, std::move(next));
    }

    void run_pane_steps(std::shared_ptr<std::vector<PaneStep>> steps, size_t index, std::function<void()> next)
    {
        if (index == steps->size()) {
            next();
            return;
        }
        const PaneStep& step = (*steps)[index];
        // The header's Home button goes to Home; Home's own page returns to
        // Prepare through the same request its backend makes.
        StatusRow* row = installed_shell()->status_row();
        if (step.tab == MainFrame::tpHome)
            row->request_home();
        else
            row->request_prepare();
        const int tab = step.tab;
        wait_until([this, tab] { return m_notebook->GetSelection() == tab; }, step.name,
                   [self = shared_from_this(), steps, index, next] {
                       (*steps)[index].on_arrival();
                       self->run_pane_steps(steps, index + 1, next);
                   });
    }

    wxSizer* frame_main_sizer()
    {
        // The harness reaches the layout through public wx state: the status
        // row was inserted into MainFrame's inner vertical sizer.
        return installed_shell()->status_row()->GetContainingSizer();
    }

    void load_multi_plate_fixture()
    {
        check(m_plater->new_project(true, true) != wxID_CANCEL, "fixture_new_project");
        const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
        const std::vector<size_t> loaded = m_plater->load_files(
            std::vector<std::string>{cube}, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence,
            false);
        check(loaded.size() == 1, "fixture_cube_loaded");
        check(m_plater->duplicate_object(0) == 1, "fixture_second_object");
        PartPlateList& plates = m_plater->get_partplate_list();
        check(plates.create_plate(true) == 1, "fixture_second_plate");
        check(plates.add_to_plate(1, 0, 1) == 0, "fixture_object_on_second_plate");
        // Slicing needs printable on-bed placement, so center each object on
        // its plate through the same path add_to_plate uses. The outside-set
        // recomputes asynchronously; slice_completes_in_preview is the
        // printability proof.
        check(plates.get_plate(0)->add_instance(0, 0, true) == 0, "fixture_object_centered_on_first_plate");
        m_plater->canvas3D()->reload_scene(true, true);
    }

    void verify_canvas_interaction()
    {
        check(m_plater->canvas3D()->get_volumes_count() >= 2, "canvas_renders_fixture");
        const wxSize canvas_size = m_plater->canvas3D()->get_wxglcanvas()->GetSize();
        check(canvas_size.GetWidth() > 200 && canvas_size.GetHeight() > 200, "canvas_has_working_area");

        check(m_plater->select_object(0), "canvas_selection_command");
        check(!m_plater->canvas3D()->get_selection().is_empty(), "canvas_selection_visible");

        installed_shell()->status_row()->refresh();
        check(!m_plater->is_preview_shown(), "starts_in_prepare");
    }

    void begin_slice_check()
    {
        auto* row = installed_shell()->status_row();
        row->request_slice();
        check(row->action_state().slicing, "header_enters_slicing");
        row->request_action(PrintAction::Cancel);
        wait_until([this] { return !m_plater->is_background_process_slicing(); }, "header_cancel_finishes",
                   [self = shared_from_this()] {
                       auto* row = installed_shell()->status_row();
                       self->check(!row->action_state().sliced, "cancel_leaves_no_valid_slice");
                       self->check(primary_print_action(row->action_state()).primary.action == PrintAction::Slice, "cancel_returns_to_slice");
                       self->begin_verified_slice();
                   });
    }

    void begin_verified_slice()
    {
        installed_shell()->status_row()->request_slice();
        wait_until([this] { return m_plater->is_background_process_slicing(); }, "header_slice_starts",
                   [self = shared_from_this()] {
                       self->wait_until(
                           [self] {
                               PartPlate* plate = self->m_plater->get_partplate_list().get_curr_plate();
                               return plate != nullptr && plate->is_slice_result_valid() && !self->m_plater->is_background_process_slicing();
                           },
                           "slice_completes", [self] { self->after_slice(); });
                   });
    }

    void after_slice()
    {
        auto* row = installed_shell()->status_row();
        check(!m_plater->is_preview_shown(), "slice_stays_in_prepare");
        check(primary_print_action(row->action_state()).primary.action == PrintAction::Print, "slice_offers_print");
        auto* primary = wxWindow::FindWindowByName("Next print action", row);
        check(primary && primary->GetLabel().StartsWith("Print"), "slice_completion_updates_rendered_button");
        const int original_plate = m_plater->get_partplate_list().get_curr_plate_index();
        m_plater->select_plate(original_plate == 0 ? 1 : 0);
        check(primary_print_action(row->action_state()).primary.action == PrintAction::Slice, "other_unsliced_plate_offers_slice");
        m_plater->select_plate(original_plate);
        check(primary_print_action(row->action_state()).primary.action == PrintAction::Print, "return_to_sliced_plate_offers_print");
        row->request_slice();
        check(!m_plater->is_background_process_slicing(), "valid_slice_cannot_reslice");
        verify_header_menus();
    }

    void choose_header_item(const wxString& name, std::function<void()> then)
    {
        auto* menu = visible_header_menu();
        auto* item = menu ? wxWindow::FindWindowByName(name,menu) : nullptr;
        if (!item || !item->IsEnabled()) throw std::runtime_error("missing enabled header menu item: " + name.ToStdString());
        for (size_t i=0;i<menu->GetChildren().size() && menu->selected_item() != item;++i) {
            wxKeyEvent down(wxEVT_CHAR); down.m_keyCode = WXK_DOWN;
            menu->GetEventHandler()->ProcessEvent(down);
        }
        check(menu->selected_item() == item,"header_menu_keyboard_selects_enabled_row");
        wxKeyEvent enter(wxEVT_CHAR); enter.m_keyCode = WXK_RETURN;
        menu->GetEventHandler()->ProcessEvent(enter);
        then();
    }

    // A mouse-opened menu must start with nothing highlighted, matching native
    // menus; a keyboard-opened one must start on the first enabled row so a
    // keyboard user has somewhere to arrow from. Keyboard runs first so the
    // mouse case proves a click clears the flag rather than reading its default.
    void verify_header_menu_open_paths(HeaderButton* chevron)
    {
        if (!chevron) return;
        auto has_enabled_item = [](HeaderMenu* menu) {
            if (!menu) return false;
            for (auto* child : menu->GetChildren())
                if (auto* item = dynamic_cast<HeaderButton*>(child); item && item->IsEnabled()) return true;
            return false;
        };
        auto dismiss = [](HeaderMenu* menu) {
            if (!menu) return;
            wxKeyEvent escape(wxEVT_CHAR_HOOK); escape.m_keyCode = WXK_ESCAPE;
            menu->GetEventHandler()->ProcessEvent(escape);
        };
        wxKeyEvent open_key(wxEVT_KEY_DOWN); open_key.m_keyCode = WXK_RETURN;
        chevron->ProcessWindowEvent(open_key);
        auto* key_menu = visible_header_menu();
        check(key_menu && bool(key_menu->selected_item()) == has_enabled_item(key_menu),
              "header_keyboard_menu_selects_an_enabled_row_when_available");
        dismiss(key_menu);

        wxMouseEvent press(wxEVT_LEFT_DOWN), release(wxEVT_LEFT_UP);
        chevron->ProcessWindowEvent(press);
        chevron->ProcessWindowEvent(release);
        auto* mouse_menu = visible_header_menu();
        check(mouse_menu && !mouse_menu->selected_item(),"header_mouse_menu_starts_unhighlighted");
        if (mouse_menu) {
            wxKeyEvent down(wxEVT_CHAR); down.m_keyCode = WXK_DOWN;
            mouse_menu->GetEventHandler()->ProcessEvent(down);
            check(bool(mouse_menu->selected_item()) == has_enabled_item(mouse_menu),
                  "header_mouse_menu_arrow_selects_an_enabled_row_when_available");
        }
        dismiss(mouse_menu);
    }

    void verify_header_menus()
    {
        auto* row = installed_shell()->status_row();
        row->show_action_menu();
        auto* menu = visible_header_menu();
        check(menu && menu->IsShown(),"header_action_menu_visible");
        check(menu && wxWindow::FindWindowByName(ui_name("Print all plates…"),menu) &&
              wxWindow::FindWindowByName(ui_name("Export sliced file…"),menu),"header_menu_has_contextual_actions");
        auto* arrow = wxWindow::FindWindowByName("Print actions",row);
        check(menu && menu->GetScreenRect().GetRight() == arrow->GetScreenRect().GetRight(),"header_menu_right_edge_matches_split_button");
        // The trigger must read as held down (and show an up chevron) for as
        // long as the menu stands, and must not stay stuck that way after.
        auto* chevron = dynamic_cast<HeaderButton*>(arrow);
        check(chevron && chevron->is_open(),"header_menu_trigger_marked_open");
        // A touching surface reads as an extension of the button whose corner
        // radius it cannot reconcile with, so the menu stands off by 4px.
        check(menu && menu->GetScreenRect().GetTop() == arrow->GetScreenRect().GetBottom()+1+row->FromDIP(4),
              "header_menu_stands_off_split_button");
        wxKeyEvent escape(wxEVT_CHAR_HOOK); escape.m_keyCode = WXK_ESCAPE;
        menu->GetEventHandler()->ProcessEvent(escape);
        check(!menu->IsShown(),"header_menu_escape_dismisses");
        check(chevron && !chevron->is_open(),"header_menu_trigger_open_state_cleared");
        verify_header_menu_open_paths(chevron);
        row->request_home();
        wait_until([this] { return m_notebook->GetSelection() == MainFrame::tpHome &&
            m_notebook->GetPage(MainFrame::tpHome)->IsShown(); },"header_home_opens_native_home",
            [self=shared_from_this()] {
                self->check(!installed_shell()->status_row()->IsShown(), "header_hidden_on_home");
                installed_shell()->status_row()->request_prepare();
                self->wait_until([self] { return self->m_notebook->GetSelection() == MainFrame::tp3DEditor && !self->m_plater->is_preview_shown(); },
                    "home_returns_to_prepare",[self] {
                        self->verify_chip_keyboard();
                        self->verify_menu_in_place_navigation();
                        self->verify_generic_preset_filter();
                        self->capture_chip_appearance();
                        self->verify_two_click_swap();
                        self->verify_header_setup(Preset::TYPE_PRINTER);
                    });
            });
    }

    // Clicks a menu row the way the platform's own pointer reaches it.
    //
    // Orca's PopupWindow installs a mouse re-dispatcher under __WXOSX__ only
    // (Widgets/PopupWindow.hpp): there the popup receives the event and
    // forwards it to the child under the pointer, so posting to the popup is
    // what a real click does. Windows delivers to the child window directly
    // and nothing forwards, so a click posted to the popup arrives nowhere and
    // the row is never invoked -- the menu simply stays as it was, which reads
    // as "the row did nothing" rather than as a broken click.
    void click_row(HeaderMenu* menu, wxWindow* row)
    {
#ifdef __WXOSX__
        wxWindow* target = menu;
        const wxPoint at = row->GetPosition() + wxPoint(row->GetSize().x / 2, row->GetSize().y / 2);
#else
        wxWindow* target = row;
        const wxPoint at(row->GetSize().x / 2, row->GetSize().y / 2);
#endif
        for (auto type : {wxEVT_LEFT_DOWN, wxEVT_LEFT_UP}) {
            wxMouseEvent mouse(type);
            mouse.SetPosition(at);
            mouse.SetEventObject(target);
            target->GetEventHandler()->ProcessEvent(mouse);
        }
    }

    // Clicks a keeps_open row and lets the deferred rebuild run. Returns the
    // menu that replaced it.
    HeaderMenu* click_menu_row(HeaderMenu* menu, const wxString& name)
    {
        auto* row = dynamic_cast<HeaderButton*>(wxWindow::FindWindowByName(name, menu));
        if (row == nullptr || !row->IsEnabled()) return nullptr;
        click_row(menu, row);
        wxYield();
        return visible_header_menu();
    }

    // The selectable rows of an open menu, in order. Rows are named by their
    // full label, which the paint-time ellipsis never shortens.
    std::vector<std::string> menu_row_names(HeaderMenu* menu) const
    {
        std::vector<std::string> names;
        for (auto* child : menu->GetChildren())
            if (auto* button = dynamic_cast<HeaderButton*>(child)) names.push_back(ui_text(button->GetName()));
        return names;
    }

    // "Generic preset…" must narrow the list by the vendor the profile
    // declares. The earlier implementation instead typed "Generic" into the
    // search box and re-ran the substring search, which matched any preset
    // whose name happened to read "generic", missed generic presets named
    // otherwise, and left a word in a field the person never typed into. Each
    // of those three is checked here, so a regression to text matching fails
    // rather than passing on a list that happens to look similar.
    void verify_generic_preset_filter()
    {
        installed_shell()->status_row()->open_spool_menu();
        auto* menu = visible_header_menu();
        check(menu != nullptr, "spool_menu_opens_for_the_generic_filter");
        if (menu == nullptr) return;

        menu = click_menu_row(menu, ui_name("Other spool…"));
        check(menu != nullptr, "other_spool_step_opens");
        if (menu == nullptr) return;
        const std::size_t unfiltered_rows = menu_row_names(menu).size();

        menu = click_menu_row(menu, ui_name("Generic preset…"));
        check(menu != nullptr, "generic_preset_step_opens");
        if (menu == nullptr) return;

        wxTextCtrl* field = nullptr;
        for (auto* child : menu->GetChildren())
            if (auto* text = dynamic_cast<wxTextCtrl*>(child); text && field == nullptr) field = text;
        check(field != nullptr && field->GetValue().empty(), "generic_step_leaves_the_search_field_empty");

        const std::vector<std::string> names = menu_row_names(menu);
        check(!names.empty() && names.front().find("GENERIC") != std::string::npos,
              "generic_step_title_says_the_list_is_filtered");
        check(names.size() < unfiltered_rows, "generic_step_shortens_the_list");

        // The listed rows must be exactly the compatible presets whose vendor
        // is Generic -- no branded preset admitted, no generic one dropped.
        std::vector<std::string> listed, expected;
        for (std::size_t i = 1; i < names.size(); ++i)
            if (names[i] != "Import a preset file…") listed.push_back(names[i]);
        std::string branded; // an alias the vendor test must have excluded
        for (const auto& filament : SetupCommands::compatible_filaments()) {
            if (filament.vendor == SetupCommands::kGenericVendor) expected.push_back(ui_text(filament.alias));
            else if (branded.empty()) branded = ui_text(filament.alias);
        }
        std::sort(listed.begin(), listed.end());
        std::sort(expected.begin(), expected.end());
        check(!expected.empty(), "the_printer_has_generic_filament_presets");
        check(listed == expected, "generic_step_lists_exactly_the_generic_vendor_presets");
        check(!branded.empty() && std::find(listed.begin(), listed.end(), branded) == listed.end(),
              "generic_step_drops_a_branded_preset");

        // Back must reach the full list again, not leave the menu.
        menu = click_menu_row(menu, ui_name(names.front()));
        check(menu != nullptr && menu_row_names(menu).size() == unfiltered_rows,
              "generic_step_returns_to_the_full_list");
        if (menu == nullptr) return;
        menu->close();
        wxYield();
        check(!chip_half_open(), "generic_step_dismisses_cleanly");
    }

    void verify_header_setup(Preset::Type type)
    {
        // The printer half opens the printer menu; the spool half's row menu
        // is where Filament settings… now lives, one step in from the list.
        if (type == Preset::TYPE_PRINTER) {
            // Printer settings… talks about the selected printer in the
            // printer conversation on Home. Only a printer saved under a name
            // can be changed there, so one is saved from the fixture's own
            // profile and selected for the length of the check.
            m_header_setup_restore_printer = wxGetApp().preset_bundle->printers.get_selected_preset_name();
            m_header_setup_printer         = Printers::add_named_printer(*m_plater, "Header Printer", {});
            check(SetupCommands::current_printer().nickname.ToStdString() == m_header_setup_printer,
                  "header_setup_named_printer_selected");
            installed_shell()->status_row()->open_printer_menu();
            choose_header_item(ui_name("Printer settings…"),
                [self=shared_from_this()] { self->verify_header_printer_settings_open(); });
            return;
        }
        installed_shell()->status_row()->open_spool_menu();
        // Row one of the spool list is the current spool; its ⋯ menu carries
        // the filament editor.
        auto* menu = visible_header_menu();
        check(menu != nullptr, "spool_menu_opens");
        if (menu == nullptr) return;
        auto* row = first_spool_row(menu);
        check(row != nullptr, "spool_menu_lists_a_spool");
        if (row == nullptr) return;
        row->invoke_row_action();
        m_frame->CallAfter([self=shared_from_this(),type] {
            self->choose_header_item(ui_name("Filament settings…"),
                [self,type] { self->verify_header_setup_open(type); });
        });
    }

    // The first selectable row of an open spool menu: the most recently used
    // remembered spool.
    HeaderButton* first_spool_row(HeaderMenu* menu) const
    {
        for (auto* child : menu->GetChildren())
            if (auto* button = dynamic_cast<HeaderButton*>(child); button && button->has_row_action())
                return button;
        return nullptr;
    }

    // Drives the two-click swap without a pointer: open the half, pick a row
    // that is not the current spool, and confirm the chip followed.
    // Writes the chip as it actually renders, in both appearance modes and in
    // the long-name case, so appearance is evidence rather than a claim. The
    // application draws these itself, so they need no screen-capture
    // permission and do not depend on what else is on the tester's display.
    void capture_chip_appearance()
    {
        auto* row  = installed_shell()->status_row();
        auto* chip = dynamic_cast<PrinterSpoolChip*>(wxWindow::FindWindowByName("Printer and spool", row));
        check(chip != nullptr, "chip_is_in_the_header");
        if (chip == nullptr) return;

        // The automated run's data directory is temporary and removed at exit.
        // JUSPRIN_ARTIFACT_DIR keeps the images for a human to look at.
        const char* artifact_dir = std::getenv("JUSPRIN_ARTIFACT_DIR");
        const fs::path out = artifact_dir != nullptr ? fs::path(artifact_dir)
                                                     : fs::path(data_dir()) / "chip-appearance";
        fs::create_directories(out);
        const bool was_dark = wxGetApp().dark_mode();
        // At rest means no menu open AND no focus ring: either one makes these
        // images describe a state the reader does not see when simply looking
        // at the header. Earlier checks leave a half focused, and a popup that
        // is still dismissing hands focus back to its anchor after it goes --
        // so drain those pending events first, then move focus away
        // unconditionally rather than testing where it happens to be.
        // The assertion below reads HasFocus(), the same predicate the painter
        // uses to draw the ring, so it cannot disagree with the image. macOS
        // grants focus only inside the key window, so it can only fire on a run
        // where this app is frontmost -- which is the run that would otherwise
        // write a chip with a ring on it.
        wxYield();
        // Focus has to land on a window that can actually hold it, and that is
        // not the row: wxPanel::SetFocus() forwards to a child -- on Windows,
        // one of the very chip halves this is clearing -- and
        // SetFocusIgnoringChildren() on a panel that is not itself focusable
        // leaves focus exactly where it was. Either way the ring the next line
        // asserts against would be the one this line drew. The canvas takes
        // focus on every platform and is not part of the header being
        // photographed.
        m_plater->canvas3D()->get_wxglcanvas()->SetFocus();
        wxYield();
        check(!chip_half_open(), "chip_is_at_rest_before_capture");
        check(!chip->printer_half().HasFocus() && !chip->spool_half().HasFocus(),
              "no_focus_ring_when_the_chip_is_captured");

        // Deliberately does NOT refresh: a refresh re-reads the spool store and
        // would overwrite any label the caller set for the shot.
        auto write = [&](const std::string& name) {
            chip->Layout();
            const wxBitmap bitmap = chip->snapshot();
            const std::string file = (out / (name + ".png")).string();
            const bool ok = bitmap.IsOk() && bitmap.GetWidth() > 0 &&
                            bitmap.ConvertToImage().SaveFile(wxString::FromUTF8(file), wxBITMAP_TYPE_PNG);
            check(ok, "chip_renders_" + name);
            if (ok) std::cout << "HARNESS ARTIFACT " << name << " " << file
                              << " " << bitmap.GetWidth() << "x" << bitmap.GetHeight() << std::endl;
            return bitmap.IsOk() ? bitmap.ConvertToImage() : wxImage();
        };
        auto same_pixels = [](const wxImage& a, const wxImage& b) {
            if (!a.IsOk() || !b.IsOk() || a.GetWidth() != b.GetWidth() || a.GetHeight() != b.GetHeight())
                return false;
            return std::memcmp(a.GetData(), b.GetData(), std::size_t(a.GetWidth()) * a.GetHeight() * 3) == 0;
        };

        row->refresh();
        installed_shell()->status_row()->apply_appearance(false);
        const wxImage light = write("light");
        installed_shell()->status_row()->apply_appearance(true);
        const wxImage dark = write("dark");
        // Dark mode is a real remapping, not the same pixels with a filter.
        check(!same_pixels(light, dark), "light_and_dark_render_differently");

        // A name far past the 240 DIP cap must ellipsize rather than widen the
        // chip. No refresh after this point: it would restore the real name.
        installed_shell()->status_row()->apply_appearance(false);
        const int natural = chip->GetBestSize().x;
        chip->set_spool("Prusament Galaxy Black PLA Blend, third reel from the shelf by the window",
                        wxColour("#101010"));
        chip->InvalidateBestSize();
        const int capped = chip->GetBestSize().x;
        const wxImage long_name = write("long-name");
        // The proof the long name was actually rendered: different pixels.
        check(!same_pixels(light, long_name), "long_name_actually_rendered");
        // 240 DIP label cap plus the half's own padding and chevron; a name
        // this long must not push the chip past that.
        check(capped <= chip->FromDIP(240) + natural, "long_spool_name_stays_within_the_cap");
        std::cout << "HARNESS MEASURE chip_natural_width=" << natural
                  << " chip_capped_width=" << capped << std::endl;

        // Leave the chip showing real state again.
        installed_shell()->status_row()->apply_appearance(was_dark);
        row->refresh();
    }

    // The nozzle, plate, other-spool and per-row steps all replace the popup's
    // own rows instead of opening a second popup. That mechanism is the one
    // thing a screenshot cannot check and a click test kept missing, so it is
    // asserted here: after activating the row, the SAME popup must still be
    // open and must now show the sub-step.
    void verify_menu_in_place_navigation()
    {
        auto* row = installed_shell()->status_row();

        row->open_printer_menu();
        auto* menu = visible_header_menu();
        check(menu != nullptr, "printer_menu_opens");
        if (menu == nullptr) return;
        auto* nozzle = wxWindow::FindWindowByName("Nozzle", menu);
        check(nozzle != nullptr, "printer_menu_has_nozzle_row");
        check(nozzle != nullptr && !nozzle->IsEnabled(), "nozzle_row_is_read_only");
        check(wxWindow::FindWindowByName("Plate", menu) != nullptr, "printer_menu_has_plate_row");

        auto* plate = static_cast<HeaderButton*>(wxWindow::FindWindowByName("Plate", menu));
        check(plate != nullptr && plate->IsEnabled(), "plate_row_is_live_with_the_sidebar_hidden");
        if (plate == nullptr || !plate->IsEnabled()) { menu->Dismiss(); return; }

        // Drive it the way a pointer does, not by synthesising the button
        // event: a real click was observed to close the menu where the button
        // event does not, so the defect lives somewhere in this path.
        click_row(menu, plate);

        // The rebuild is deferred past the click, so let the queue drain.
        wxYield();
        auto* after = visible_header_menu();
        check(after != nullptr, "plate_substep_keeps_the_menu_open_after_a_click");
        if (after == nullptr) return;
        // The sub-step replaced the rows: the root's Nozzle row is gone and a
        // back row named Plate heads the list.
        check(wxWindow::FindWindowByName("Nozzle", after) == nullptr, "plate_substep_replaced_the_rows");
        check(wxWindow::FindWindowByName(ui_name("Printer settings…"), after) == nullptr, "plate_substep_hides_root_rows");
        after->close();
        wxYield();
        // Dismissing must also clear the anchor's open state, or the chip keeps
        // painting its pressed fill with no menu on screen.
        check(!chip_half_open(), "dismissing_the_menu_clears_the_chip_open_state");
    }

    // True while either chip half still believes its menu is open.
    bool chip_half_open() const
    {
        auto* chip = dynamic_cast<PrinterSpoolChip*>(
            wxWindow::FindWindowByName("Printer and spool", installed_shell()->status_row()));
        return chip != nullptr && (chip->printer_half().is_open() || chip->spool_half().is_open());
    }

    // Left/right arrows move between the chip's halves, so the whole chip
    // behaves like one control to the keyboard while remaining two to the
    // pointer. Focus is asserted, not the key press.
    void verify_chip_keyboard()
    {
        auto* row  = installed_shell()->status_row();
        auto* chip = dynamic_cast<PrinterSpoolChip*>(wxWindow::FindWindowByName("Printer and spool", row));
        check(chip != nullptr, "chip_present_for_keyboard");
        if (chip == nullptr) return;

        // Focus only sticks while the top-level window is active, so make it
        // so before asserting; otherwise this measures window activation.
        m_frame->Raise();
        m_frame->SetFocus();
        wxYield();
        chip->printer_half().SetFocus();
        wxYield();
        if (!chip->printer_half().HasFocus()) {
            // The harness window is not key (common when another app is
            // frontmost). Focus assertions would measure the environment, not
            // the chip, so say that plainly instead of failing or pretending.
            std::cout << "HARNESS SKIP chip_keyboard_needs_an_active_window" << std::endl;
            return;
        }
        check(chip->printer_half().HasFocus(), "printer_half_takes_focus");

        auto arrow = [&](wxWindow& from, int key) {
            wxKeyEvent event(wxEVT_KEY_DOWN);
            event.m_keyCode = key;
            event.SetEventObject(&from);
            from.GetEventHandler()->ProcessEvent(event);
            wxYield();
        };
        arrow(chip->printer_half(), WXK_RIGHT);
        check(chip->spool_half().HasFocus(), "right_arrow_moves_to_the_spool_half");
        arrow(chip->spool_half(), WXK_LEFT);
        check(chip->printer_half().HasFocus(), "left_arrow_moves_back_to_the_printer_half");
    }

    // The after-swap line must reach the page as a note, rendered without a
    // bubble. Checked in the DOM, because the host storing it proves nothing
    // about what the reader sees. RunScript cannot return a value on macOS, so
    // the probe reports through the composer draft, which the host owns.
    void verify_note_rendered(std::function<void()> then)
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        WebView::RunScript(web_view.webview(),
            "(function(){"
            "  var notes = document.querySelectorAll('.message.note');"
            "  var bubbles = document.querySelectorAll('.message.note.user, .message.note.assistant');"
            "  var text = notes.length ? notes[0].textContent : '';"
            "  var probe = 'notes=' + notes.length + ';bubbles=' + bubbles.length + ';first=' + text;"
            "  window.__jusprinTest && window.__jusprinTest.setDraft(probe);"
            "})()");
        wait_until([this] { return persistence().draft().rfind("notes=", 0) == 0; },
            "note_probe_reported", [self = shared_from_this(), then] {
                const std::string probe = self->persistence().draft();
                self->check(probe.find("notes=0;") == std::string::npos, "page_renders_the_swap_notes");
                self->check(probe.find(";bubbles=0;") != std::string::npos, "notes_render_without_a_bubble");
                self->check(probe.find("Harness Second Spool") != std::string::npos ||
                            probe.find("Harness Other Preset") != std::string::npos,
                            "the_rendered_note_names_the_spool");
                std::cout << "HARNESS NOTE PROBE " << probe << std::endl;
                self->persistence().set_draft({});
                then();
            });
    }

    void verify_two_click_swap()
    {
        auto* row = installed_shell()->status_row();
        auto spools = row->listed_spools();
        check(!spools.empty(), "chip_seeds_a_spool_for_this_printer");
        if (spools.empty()) return;

        // A fresh profile has exactly the seeded spool. Remember a second one
        // -- same preset, a different colour, the ordinary "I loaded the other
        // reel" case -- so the swap under test is a real swap.
        const std::string other_colour = spools.front().colour == "#101010" ? "#F5F5F0" : "#101010";
        const auto second = row->remember_spool(spools.front().filament_preset, other_colour, "Harness Second Spool");
        check(row->listed_spools().size() == spools.size() + 1, "remembering_a_spool_adds_one_row");

        const wxString before = chip_label(row, "Spool");
        check(row->select_spool(second.id), "select_spool_applies_a_remembered_spool");
        check(chip_label(row, "Spool") == wxString::FromUTF8(second.name), "chip_shows_the_swapped_spool");
        check(chip_label(row, "Spool") != before, "the_chip_actually_changed");
        // The project itself moved, not just the label.
        check(SetupCommands::current_colour().Lower() == wxString::FromUTF8(other_colour).Lower(),
              "swap_writes_the_colour_into_the_project");
        // The swapped-to spool is now the most recently used.
        check(row->listed_spools().front().id == second.id, "swap_stamps_recency");
        // And it is the one the chip reports as current.
        const auto current = row->current_spool();
        check(current.has_value() && current->id == second.id, "current_spool_matches_after_a_swap");

        check(!row->select_spool("not-a-spool-id"), "select_spool_refuses_an_unknown_id");

        // Swapping to a DIFFERENT filament preset is a heavier path than
        // recolouring the same one: it runs Orca's preset switch, which can
        // touch compatibility, dirty state and the sidebar. The "Use this
        // spool" step does exactly this, so it is exercised here.
        const auto compatible = SetupCommands::compatible_filaments();
        const std::string current_preset = SetupCommands::current_filament().preset_name;
        std::string other_preset;
        for (const auto& filament : compatible)
            if (filament.preset_name != current_preset) { other_preset = filament.preset_name; break; }
        check(!other_preset.empty(), "a_second_compatible_filament_exists");
        if (other_preset.empty()) return;

        const auto crossed = row->remember_spool(other_preset, "#101010", "Harness Other Preset");
        check(row->select_spool(crossed.id), "select_spool_switches_to_another_preset");
        check(SetupCommands::current_filament().preset_name == other_preset,
              "the_project_is_on_the_new_filament_preset");
        check(chip_label(row, "Spool") == wxString::FromUTF8(crossed.name), "chip_shows_the_cross_preset_spool");
    }

    // Since 0e55c4f9be the header's Printer settings… opens the printer
    // conversation on Home, not Orca's settings window.
    void verify_header_printer_settings_open()
    {
        wait_until([this] {
                PrinterSetup::PrinterPanel* panel = installed_shell()->printer_panel();
                return panel != nullptr && panel->IsShown() && m_notebook->GetSelection() == MainFrame::tpHome;
            }, "header_printer_settings_opens_printer_conversation_on_home",
            [self=shared_from_this()] {
                PrinterSetup::PrinterPanel* panel = installed_shell()->printer_panel();
                const nlohmann::json session = panel->session_json();
                self->check(session.value("mode", "") == "change" &&
                                session.value("printerName", "") == self->m_header_setup_printer,
                            "header_printer_settings_is_about_the_selected_printer");
                self->check(!wxGetApp().params_dialog()->IsShown(), "header_printer_settings_leaves_settings_window_closed");
                panel->close();
                self->wait_until([panel] { return !panel->IsShown(); }, "header_printer_conversation_closes",
                    [self] {
                        installed_shell()->status_row()->request_prepare();
                        self->wait_until([self] { return self->m_notebook->GetSelection() == MainFrame::tp3DEditor &&
                                                         !self->m_plater->is_preview_shown(); },
                            "header_printer_settings_returns_to_prepare", [self] {
                                self->check(Printers::remove_named_printer(*self->m_plater, nullptr,
                                                                          self->m_header_setup_printer).empty(),
                                            "header_setup_named_printer_removed");
                                SetupCommands::select_printer_preset(*self->m_plater, self->m_header_setup_restore_printer);
                                self->check(wxGetApp().preset_bundle->printers.get_selected_preset_name() ==
                                                self->m_header_setup_restore_printer,
                                            "header_setup_fixture_printer_restored");
                                self->verify_header_setup(Preset::TYPE_FILAMENT);
                            });
                    });
            });
    }

    // Filament settings… still opens Orca's settings window on its tab.
    void verify_header_setup_open(Preset::Type type)
    {
        const std::string kind = "filament";
        wait_until([] { return wxGetApp().params_dialog()->IsShown(); },"header_setup_opens_" + kind + "_editor",
            [self=shared_from_this(),type,kind] {
                self->check(wxGetApp().params_dialog()->panel()->get_current_tab() == wxGetApp().get_tab(type),
                            "header_setup_selects_" + kind + "_tab");
                wxGetApp().params_dialog()->Close();
                self->verify_header_overflow();
            });
    }

    void verify_header_overflow()
    {
        installed_shell()->status_row()->show_overflow_menu();
        choose_header_item("Project details",[self=shared_from_this()] { self->verify_project_details_open(); });
    }

    void verify_project_details_open()
    {
        wait_until([this] { return m_notebook->GetSelection() == MainFrame::tpProject; },
            "header_overflow_opens_project_details",[self=shared_from_this()] {
                installed_shell()->status_row()->request_prepare();
                self->wait_until([self] { return self->m_notebook->GetSelection() == MainFrame::tp3DEditor; },
                    "header_navigation_keeps_project",[self] {
                        self->check(self->m_plater->model().objects.size() >= 2,"header_navigation_preserves_objects");
                        self->verify_print_preflight(PrintAction::Print);
                        self->begin_slice_all_warm();
                    });
            });
    }

    HeaderMenu* visible_header_menu() const
    {
        // Dismissed popups remain in the wx tree until delayed destruction.
        // Only the currently shown popup represents a user-visible menu.
        for (auto* child : installed_shell()->status_row()->GetChildren())
            if (auto* menu = dynamic_cast<HeaderMenu*>(child); menu && menu->IsShown()) return menu;
        return nullptr;
    }

    void verify_print_preflight(PrintAction action)
    {
        // Close only the expected native confirmation with Cancel. No printer
        // is selected and no Send button is ever invoked by this test.
        bool observed = false;
        wxEvtHandler handler;
        wxTimer timer(&handler);
        handler.Bind(wxEVT_TIMER, [&](wxTimerEvent&) {
            for (auto* window : wxTopLevelWindows) {
                auto* dialog = dynamic_cast<wxDialog*>(window);
                if (dialog && dialog->IsModal() && dialog->GetTitle() == "Send print job") {
                    observed = true;
                    dialog->EndModal(wxID_CANCEL);
                    timer.Stop();
                    return;
                }
            }
        });
        timer.Start(50);
        installed_shell()->status_row()->request_action(action);
        timer.Stop();
        check(observed, action == PrintAction::Print ? "print_opens_cancellable_native_preflight" : "print_all_opens_cancellable_native_preflight");
    }

    bool all_nonempty_plates_sliced()
    {
        for (PartPlate* plate : m_plater->get_partplate_list().get_nonempty_plate_list())
            if (plate == nullptr || !plate->is_slice_result_valid())
                return false;
        return true;
    }

    // Slice all with the Preview canvas already initialized by the earlier
    // single-plate slice. Guards the interaction between the shell's hidden
    // plate toolbar and upstream's all-plates stats item (see 83723e2a7b):
    // the same menu dispatch must slice every nonempty plate without crashing.
    void begin_slice_all_warm()
    {
        auto& plates = m_plater->get_partplate_list();
        for (int i = 0; i < plates.get_plate_count(); ++i)
            if (!plates.get_plate(i)->is_slice_result_valid()) { m_plater->select_plate(i); break; }
        installed_shell()->status_row()->request_action(PrintAction::SliceAll);
        wait_until([this] { return all_nonempty_plates_sliced() && !m_plater->is_background_process_slicing(); }, "slice_all_completes_all_plates",
                   [self = shared_from_this()] {
                       self->check(!self->m_plater->is_preview_shown(), "slice_all_stays_in_prepare");
                       self->check(!self->m_plater->get_preview_canvas3D()->is_all_plates_selected(), "slice_all_does_not_select_hidden_preview");
                       self->verify_print_preflight(PrintAction::PrintAll);
                       // Restore the deterministic first-plate state for later phases.
                       self->m_plater->select_plate(0);
                       self->verify_agent_bridge();
                   });
    }

    // Opt-in --slice-all-cold: same dispatch, but before the Preview canvas
    // has ever rendered. See the mode comment at the top of this file.
    void begin_slice_all_cold()
    {
        check(!m_plater->is_preview_shown(), "cold_starts_in_prepare");
        check(m_plater->get_partplate_list().get_nonempty_plate_list().size() > 1, "cold_fixture_has_multiple_plates");
        installed_shell()->status_row()->request_slice(true);
        wait_until([this] { return all_nonempty_plates_sliced(); }, "cold_slice_all_completes_all_plates",
                   [self = shared_from_this()] {
                       self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "cold_teardown_project");
                       self->finish();
                   });
    }

    // The Agent page's failure surface is its own Retry pane, and no fixture
    // mode looked at it: a WebView2 controller that failed to come up
    // (Windows ERROR_INVALID_STATE, delivered as wxWEBVIEW_NAV_ERR_OTHER) left
    // the pane on "could not connect" while the run reported failures=0.
    // AgentWebView shows that pane on a load error at once and on a silent
    // page after kHandshakeDeadlineMs (20 s), so "handshake or error pane" is
    // bounded by that deadline; the margin here is only for a page that
    // neither connects nor admits it. Automated modes stop at once instead of
    // spending the 900 s run deadline on a page that is not coming; fixture
    // modes still hand over, with the failure counted, so it can be looked at.
    void wait_for_agent_page(const std::string& prefix, std::function<void()> next)
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        check(web_view.webview() != nullptr, prefix + "_agent_webview_created");
        const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        wait_until([&web_view, give_up] {
            return web_view.host().handshake_complete() || web_view.bridge_error_shown() ||
                   std::chrono::steady_clock::now() >= give_up;
        }, prefix + "_agent_page_settled", [self = shared_from_this(), prefix, next = std::move(next)] {
            AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
            const bool up = web_view.host().handshake_complete();
            self->check(up, prefix + "_agent_page_handshake_complete");
            self->check(!web_view.bridge_error_shown(), prefix + "_agent_page_shows_no_bridge_error");
            if (!up && self->m_state->mode != HarnessState::Mode::ManualMcp) {
                self->fail(prefix + ": the Agent page did not complete its handshake");
                return;
            }
            next();
        });
    }

    // Handoff item 6. The setup card's "recomputing" state exists only while
    // a background slice runs on a plate that already has an estimate to
    // strike through (OrcaWorkspaceAdapter::snapshot, the remembered-estimate
    // branch), and a 20 mm cube slices faster than a person can photograph
    // it. So the second slice is made slow -- the same cube at 6x is 120 mm
    // on a side, 600 layers instead of 100 -- and the pictures are taken from
    // in here, at the moment request_slice() returns.
    void begin_recomputing_capture()
    {
        installed_shell()->status_row()->request_slice();
        wait_until([this] { return active_plate_sliced_and_idle(); }, "recomputing_first_slice_completes",
                   [self = shared_from_this()] { self->reslice_for_recomputing(); });
    }

    bool active_plate_sliced_and_idle() const
    {
        const PartPlate* plate = m_plater->get_partplate_list().get_curr_plate();
        return plate != nullptr && plate->is_slice_result_valid() && !m_plater->is_background_process_slicing();
    }

    std::optional<Workspace::WorkspacePlate> active_workspace_plate() const
    {
        const auto snapshot = installed_workspace_snapshot();
        for (const auto& plate : snapshot.plates)
            if (plate.active) return plate;
        return std::nullopt;
    }

    void reslice_for_recomputing()
    {
        // The estimate the card strikes through is remembered only when a
        // snapshot is read while the slice is valid and idle. The page reads
        // one on its own; reading one here makes that a check, not a race.
        const auto before = active_workspace_plate();
        check(before && before->estimate.has_value() &&
              before->estimate_status == Workspace::EstimateStatus::Current,
              "recomputing_plate_has_a_current_estimate");

        // The instance transform is what the scale gizmo changes, so Orca's
        // own apply() sees a model change and invalidates the plate through
        // the path a person's edit takes.
        ModelObject* object = m_plater->model().objects.front();
        object->instances.front()->set_scaling_factor(Vec3d(6.0, 6.0, 6.0));
        object->ensure_on_bed();
        m_plater->update(false, true);
        auto* row = installed_shell()->status_row();
        row->refresh();
        check(!row->action_state().sliced, "recomputing_scale_invalidates_the_slice");

        row->request_slice();
        check(m_plater->is_background_process_slicing(), "recomputing_reslice_starts");
        // Native truth first, so this cannot pass on a plate that never left
        // "stale".
        const auto during = active_workspace_plate();
        check(during && during->estimate_status == Workspace::EstimateStatus::Recomputing,
              "workspace_reports_recomputing_while_slicing");
        // Then the page, which learns of the slice over the bridge. The probe
        // is asked on every tick and the wait ends the moment the slice does,
        // so a slice that beats the page is a failure and not a hang.
        persistence().set_draft({});
        wait_until([this] {
            return persistence().draft().rfind("card=", 0) == 0 || !m_plater->is_background_process_slicing();
        }, "recomputing_card_probe_settled",
        [self = shared_from_this()] { self->capture_recomputing_card(); },
        [] { probe_setup_card(); });
    }

    // Reports only once the card carries the recomputing markup
    // (SetupCard.tsx: .superseded is the struck estimate, .estimate-note is
    // "re-slicing…"), so an unanswered probe and a stale card look the same:
    // no draft.
    static void probe_setup_card()
    {
        WebView::RunScript(installed_shell()->agent_pane()->web_view().webview(),
            "(function(){"
            "  var card = document.querySelector('[data-testid=\"current-setup\"]');"
            "  var note = card && card.querySelector('.estimate-note');"
            "  var struck = card && card.querySelector('.superseded');"
            "  if (note && struck && window.__jusprinTest)"
            "    window.__jusprinTest.setDraft('card=' + note.textContent + '|' + struck.textContent);"
            "})()");
    }

    void capture_recomputing_card()
    {
        const std::string probe = persistence().draft();
        const bool observed = probe.rfind("card=", 0) == 0;
        check(observed, "setup_card_renders_recomputing_while_slicing");
        check(observed && probe.find("re-slicing") != std::string::npos, "setup_card_note_reads_re_slicing");
        std::cout << "HARNESS CARD PROBE " << probe << std::endl;
        persistence().set_draft({});
        // The pictures must show the state the probe saw.
        check(m_plater->is_background_process_slicing(), "recomputing_still_slicing_at_capture");
        wxYield();
        write_screen_capture(installed_shell()->agent_pane()->GetScreenRect(), "recomputing-agent-pane");
        write_screen_capture(m_frame->GetScreenRect(), "recomputing-shell");
        wait_until([this] { return active_plate_sliced_and_idle(); }, "recomputing_reslice_completes",
                   [self = shared_from_this()] {
                       const auto after = self->active_workspace_plate();
                       self->check(after && after->estimate_status == Workspace::EstimateStatus::Current,
                                   "recomputing_returns_to_current");
                       self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "recomputing_teardown_project");
                       self->finish();
                   });
    }

    // Revision-timeline handoff B10: the thread the Figma frame
    // "print-timeline-panel-full · no revert" draws, built from real edits so
    // the rows come through the adapter, persistence and the bridge exactly
    // as a person's would. A recorded build gives a history card, a question
    // the Agent answers without a change gives the "nothing changed" marker,
    // three mirrors give one merged row, and a Tab edit gives a setting row.
    void begin_timeline_capture()
    {
        installed_shell()->status_row()->request_slice();
        wait_until([this] { return active_plate_sliced_and_idle(); }, "timeline_fixture_sliced",
                   [self = shared_from_this()] { self->timeline_record_build(); });
    }

    void timeline_record_build()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        const std::size_t activities_before = web_view.host().tools().activities().size();
        WebView::RunScript(web_view.webview(), "window.__jusprinTest && window.__jusprinTest.send('/build')");
        wait_until([&web_view, activities_before] {
            const auto& activities = web_view.host().tools().activities();
            return activities.size() > activities_before && activities.back().tool == "record_build" &&
                   activities.back().state == Agent::ToolState::Pending;
        }, "timeline_build_proposed", [self = shared_from_this()] {
            AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
            const std::string action_id = web_view.host().tools().activities().back().action_id;
            WebView::RunScript(web_view.webview(),
                               wxString::FromUTF8("window.__jusprinTest.decide('" + action_id + "', 'approve')"));
            self->wait_until([self, &web_view] {
                return self->persistence().document().builds().size() == 1 && !web_view.host().stream_active();
            }, "timeline_build_recorded", [self] { self->timeline_ask(); });
        });
    }

    void timeline_ask()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        const std::size_t before = web_view.host().conversation().size();
        WebView::RunScript(web_view.webview(), "window.__jusprinTest.send('Will it need supports?')");
        wait_until([&web_view, before] {
            const auto messages = web_view.host().conversation();
            return messages.size() >= before + 2 && messages.back().role == Agent::MessageRole::Assistant &&
                   messages.back().state == Agent::MessageState::Complete;
        }, "timeline_reply_completes", [self = shared_from_this()] { self->timeline_edit_by_hand(); });
    }

    void timeline_edit_by_hand()
    {
        const std::size_t changes_before = persistence().document().changes().size();
        check(m_plater->select_object(0), "timeline_object_selected");
        for (int mirror = 0; mirror < 3; ++mirror)
            m_plater->mirror(Slic3r::X);
        const std::string density =
            wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_serialize("sparse_infill_density") == "35%" ?
                "45%" : "35%";
        DynamicPrintConfig diff;
        diff.set_deserialize_strict("sparse_infill_density", density);
        wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(diff);

        const auto changes = persistence().document().changes();
        const auto added = [&](const std::string& kind) {
            return std::count_if(changes.begin() + static_cast<std::ptrdiff_t>(changes_before), changes.end(),
                                 [&kind](const Agent::ChangeEntry& change) { return change.kind == kind; });
        };
        check(added("step") == 3, "timeline_three_mirrors_are_three_steps");
        check(added("setting") >= 1, "timeline_tab_edit_is_a_setting_change");
        check(std::all_of(changes.begin() + static_cast<std::ptrdiff_t>(changes_before), changes.end(),
                          [](const Agent::ChangeEntry& change) { return change.actor == "person"; }),
              "timeline_hand_edits_are_the_persons");

        persistence().set_draft({});
        wait_until([this] { return persistence().draft().rfind("timeline=", 0) == 0; }, "timeline_rows_rendered",
                   [self = shared_from_this()] { self->capture_timeline(); }, [] { probe_timeline(); });
    }

    // Reports the rendered rows once the merged mirror row is on the page, so
    // an unanswered probe and a page still catching up look the same: no draft.
    static void probe_timeline()
    {
        WebView::RunScript(installed_shell()->agent_pane()->web_view().webview(),
            "(function(){"
            "  var rows = Array.prototype.map.call(document.querySelectorAll('.change-row'),"
            "    function (row) { return row.textContent; });"
            "  var answered = document.querySelectorAll('.answered-state').length;"
            "  if (window.__jusprinTest && rows.join('|').indexOf('3 steps merged') >= 0)"
            "    window.__jusprinTest.setDraft('timeline=' + answered + '|' + rows.join('|'));"
            "})()");
    }

    void capture_timeline()
    {
        const std::string probe = persistence().draft();
        std::cout << "HARNESS TIMELINE PROBE " << probe << std::endl;
        check(probe.rfind("timeline=", 0) == 0 && probe.substr(9, 1) != "0", "timeline_reply_says_nothing_changed");
        check(probe.find("3 steps merged") != std::string::npos, "timeline_mirrors_merge_into_one_row");
        check(probe.find("\xE2\x86\x92") != std::string::npos, "timeline_setting_row_reads_from_to");
        persistence().set_draft({});
        wait_until([this] { return persistence().draft().rfind("spacing=", 0) == 0; }, "timeline_spacing_measured",
                   [self = shared_from_this()] { self->check_timeline_spacing(); }, [] { probe_spacing(); });
    }

    // The thread's spacing as laid out, against the corrected Figma frame:
    // items 12 apart; padding 4 top, 16 sides and bottom; 8 inside a turn;
    // 4 from the reply's last line of text to "Answered · nothing changed";
    // and 12 from the turn to the change run after it.
    static void probe_spacing()
    {
        WebView::RunScript(installed_shell()->agent_pane()->web_view().webview(),
            "(function(){"
            "  var list = document.querySelector('.message-list');"
            "  if (!list || !window.__jusprinTest) return;"
            // The reply the hand edits follow; the /build reply before it is
            // answered too, but a history card follows that one.
            "  var turn = Array.prototype.filter.call(document.querySelectorAll('.answered-turn'), function (t) {"
            "    var n = t.parentElement.nextElementSibling; return n && n.className === 'change-rows'; })[0];"
            "  if (!turn) return;"
            "  var group = turn.parentElement, next = group.nextElementSibling;"
            "  var s = getComputedStyle(list);"
            "  var text = document.createRange();"
            "  text.selectNodeContents(turn.querySelector('.message-content'));"
            "  var answered = turn.querySelector('.answered-state').getBoundingClientRect();"
            "  window.__jusprinTest.setDraft('spacing=' + [s.rowGap, s.paddingTop, s.paddingRight, s.paddingBottom,"
            "    s.paddingLeft, getComputedStyle(group).rowGap,"
            "    Math.round(answered.top - text.getBoundingClientRect().bottom),"
            "    Math.round(next.getBoundingClientRect().top - group.getBoundingClientRect().bottom)].join(','));"
            "})()");
    }

    void check_timeline_spacing()
    {
        const std::string spacing = persistence().draft();
        std::cout << "HARNESS TIMELINE SPACING " << spacing << std::endl;
        check(spacing == "spacing=12px,4px,16px,16px,16px,8px,4,12", "timeline_spacing_matches_figma");
        persistence().set_draft({});
        // The thread follows new content, but a late reflow can leave the last
        // row just below the fold; the picture must show the newest rows.
        WebView::RunScript(installed_shell()->agent_pane()->web_view().webview(),
                           "(function(){ var list = document.querySelector('.message-list');"
                           "  if (list) list.scrollTop = list.scrollHeight; })()");
        auto ticks = std::make_shared<int>(0);
        wait_until([ticks] { return ++*ticks > 10; }, "timeline_thread_scrolled_to_end", [self = shared_from_this()] {
            wxYield();
            const bool dark = self->m_state->dark_appearance.value_or(false);
            self->write_screen_capture(installed_shell()->agent_pane()->GetScreenRect(),
                                       std::string("timeline-agent-pane-") + (dark ? "dark" : "light"));
            self->timeline_new_project_starts_fresh();
        });
    }

    // OrcaSlicer's undo timestamps restart with a new project, so the adapter
    // must reset what it has seen: the new project's log starts empty, and
    // its first edit is its first entry.
    void timeline_new_project_starts_fresh()
    {
        const std::string before = persistence().document().project_id();
        check(m_plater->new_project(true, true) != wxID_CANCEL, "timeline_teardown_project");
        wait_until([this, before] {
            return persistence().document().has_identity() && persistence().document().project_id() != before;
        }, "timeline_new_project_adopted", [self = shared_from_this()] {
            self->check(self->persistence().document().changes().empty(), "timeline_new_project_starts_an_empty_change_log");
            const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
            self->m_plater->load_files(std::vector<std::string>{cube},
                                       LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence,
                                       false);
            const std::size_t after_load = self->persistence().document().changes().size();
            for (const Agent::ChangeEntry& change : self->persistence().document().changes())
                std::cout << "HARNESS TIMELINE AFTER LOAD " << change.kind << " '" << change.label << "'" << std::endl;
            // A real undo step in the new project. Its timestamp restarts
            // from zero with the new history; had the adapter kept the old
            // project's baseline, this step would go unreported.
            self->check(self->m_plater->select_object(0), "timeline_new_project_object_selected");
            self->m_plater->mirror(Slic3r::X);
            const auto changes = self->persistence().document().changes();
            self->check(changes.size() == after_load + 1 && changes.back().kind == "step",
                        "timeline_first_edit_after_new_project_is_logged");
            self->finish();
        });
    }

    // What is on the screen, not what a widget would draw: the card is
    // WebView2 content, which no snapshot() can paint offscreen. Needs the
    // frame on a visible desktop; at 100% scaling GetScreenRect() and the
    // screen DC share pixels (see handoff item 4 before trusting 150%/200%).
    // The Prepare canvas's tool strip. Pointer events go to the canvas at a
    // button's centre and pass through GLCanvas3D::on_mouse and ImGui's hit
    // test as a user's click does; every result is read back from the gizmo
    // manager, the selection and the model, never from the strip.
    GLCanvas3D& prepare_canvas() { return *m_plater->canvas3D(); }

    GLGizmosManager::EType open_tool() { return prepare_canvas().get_gizmos_manager().get_current_type(); }

    StripLayout tool_strip_layout()
    {
        const Size size = prepare_canvas().get_canvas_size();
        return layout_tool_strip(tool_strip_geometry(brand_theme()->metrics()), wxGetApp().imgui()->get_style_scaling(),
                                 float(size.get_width()), float(size.get_height()));
    }

    void send_canvas_mouse(wxEventType type, const wxPoint& at, bool left_down)
    {
        wxGLCanvas* target = prepare_canvas().get_wxglcanvas();
        wxMouseEvent event(type);
        event.SetEventObject(target);
        event.SetPosition(at);
        event.m_leftDown = left_down;
        target->GetEventHandler()->ProcessEvent(event);
    }

    // A click the way a pointer makes one on a strip already on screen: a
    // frame shows the strip (ImGui hit-tests against the previous frame's
    // windows), the pointer arrives and a frame is drawn so ImGui knows what
    // is under it, then the button goes down and up.
    void press_tool_strip(StripTool tool)
    {
        prepare_canvas().render();
        const StripRect& button = tool_strip_layout().buttons[size_t(tool)];
        const double scale = prepare_canvas().get_scale();
        const wxPoint at(int((button.x + button.w / 2) / scale), int((button.y + button.h / 2) / scale));
        send_canvas_mouse(wxEVT_MOTION, at, false);
        prepare_canvas().render();
        send_canvas_mouse(wxEVT_LEFT_DOWN, at, true);
        send_canvas_mouse(wxEVT_LEFT_UP, at, false);
    }

    // The strip runs its action after the frame, so callers wait for the loop.
    void click_tool_strip(StripTool tool, const std::string& settled, std::function<void()> then)
    {
        press_tool_strip(tool);
        wait_until_settled(settled, std::move(then));
    }

    // The whole window, not the canvas: the canvas's own screen rectangle
    // comes back window-relative here, which photographs the wrong part of
    // the screen, while the frame's is right. The window also shows the strip
    // against the rest of the shell, which is what these pictures are for.
    void capture_tool_strip(const std::string& name)
    {
        prepare_canvas().render();
        const bool dark = m_state->dark_appearance.value_or(false);
        write_screen_capture(m_frame->GetScreenRect(), "tool-strip-" + name + (dark ? "-dark" : "-light"));
    }

    size_t first_object_instances() { return m_plater->model().objects.front()->instances.size(); }

    // The open tool's value card is an ImGui window of ours, so its rectangle
    // is the anchor contract: it hangs a panel padding below the strip with
    // its left edge under the button that opened it.
    void check_tool_panel_anchored(StripTool tool, const std::string& name)
    {
        prepare_canvas().render();
        const ImGuiWindow* window = ImGui::FindWindowByName("##jusprin_tool_values");
        check(window != nullptr && window->Active, name + "_panel_is_open");
        if (window == nullptr || !window->Active)
            return;
        const StripLayout layout = tool_strip_layout();
        const StripRect& button = layout.buttons[size_t(tool)];
        const float padding = std::round(brand_theme()->metrics().tool_panel.padding *
                                         wxGetApp().imgui()->get_style_scaling());
        check(std::abs(window->Pos.x - button.x) <= 1.f, name + "_panel_left_edge_under_the_button");
        check(std::abs(window->Pos.y - (layout.strip.y + layout.strip.h + padding)) <= 1.f,
              name + "_panel_hangs_below_the_strip");
        check(window->Size.x > 0.f && window->Size.y > 0.f, name + "_panel_has_content");
    }

    void tool_strip_idle()
    {
        check(open_tool() == GLGizmosManager::Undefined, "tool_strip_starts_with_no_tool_open");
        m_tool_strip_selection = prepare_canvas().get_selection().get_volume_idxs();
        capture_tool_strip("selected");
        click_tool_strip(StripTool::Move, "tool_strip_move_click_settled", [self = shared_from_this()] {
            self->check(self->open_tool() == GLGizmosManager::Move, "tool_strip_opens_move");
            self->check(self->prepare_canvas().get_selection().get_volume_idxs() == self->m_tool_strip_selection,
                        "tool_strip_click_keeps_the_selection");
            self->check_tool_panel_anchored(StripTool::Move, "move");
            self->capture_tool_strip("move");
            self->click_tool_strip(StripTool::Move, "tool_strip_second_move_click_settled", [self] {
                self->check(self->open_tool() == GLGizmosManager::Undefined, "tool_strip_second_click_closes_move");
                self->tool_strip_shortcut();
            });
        });
    }

    // A tool opened from the keyboard must light its button: the strip reads
    // the open tool every frame instead of remembering its own clicks.
    void tool_strip_shortcut()
    {
        wxGLCanvas* target = prepare_canvas().get_wxglcanvas();
        wxKeyEvent key(wxEVT_CHAR);
        key.SetEventObject(target);
        key.m_keyCode = 'r';
        target->GetEventHandler()->ProcessEvent(key);
        wait_until_settled("tool_strip_shortcut_settled", [self = shared_from_this()] {
            self->check(self->open_tool() == GLGizmosManager::Rotate, "r_key_opens_rotate");
            self->check_tool_panel_anchored(StripTool::Rotate, "rotate");
            self->capture_tool_strip("rotate-shortcut");
            self->tool_strip_typed_rotation();
        });
    }

    // Typing into a field, the way a person does it: click the field, type
    // the digits, press Enter. The result is read from the model, not from
    // the card, so a value that displays but never applies still fails.
    void type_into_field(const char* field, const std::string& digits)
    {
        prepare_canvas().render();
        const ViewportToolStrip* strip = installed_shell()->prepare_canvas_presentation().tool_strip();
        check(strip != nullptr, "tool_strip_is_installed");
        if (strip == nullptr)
            return;
        const auto& rects = strip->values().field_rects();
        const auto found = rects.find(field);
        check(found != rects.end(), std::string("field_") + field + "_was_drawn");
        if (found == rects.end())
            return;
        const double scale = prepare_canvas().get_scale();
        const wxPoint at(int((found->second.x + found->second.w / 2) / scale),
                         int((found->second.y + found->second.h / 2) / scale));
        send_canvas_mouse(wxEVT_MOTION, at, false);
        prepare_canvas().render();
        send_canvas_mouse(wxEVT_LEFT_DOWN, at, true);
        send_canvas_mouse(wxEVT_LEFT_UP, at, false);
        prepare_canvas().render();

        wxGLCanvas* target = prepare_canvas().get_wxglcanvas();
        const auto send_char = [target](int code) {
            wxKeyEvent event(wxEVT_CHAR);
            event.SetEventObject(target);
            event.m_keyCode = code;
            event.m_uniChar = code;
            target->GetEventHandler()->ProcessEvent(event);
        };
        // A key, not a character: ImGui reads Enter from the key state, which
        // only wxEVT_KEY_DOWN and KEY_UP carry, so a char alone leaves the
        // field open and the value uncommitted.
        const auto send_key = [target](int code) {
            for (const wxEventType type : {wxEVT_KEY_DOWN, wxEVT_KEY_UP}) {
                wxKeyEvent event(type);
                event.SetEventObject(target);
                event.m_keyCode = code;
                target->GetEventHandler()->ProcessEvent(event);
            }
        };
        for (const char digit : digits)
            send_char(digit);
        prepare_canvas().render();
        // While the caret is in the field: the focus ring and the axis hint.
        if (m_capture_typing) {
            capture_tool_strip("typing");
            m_capture_typing = false;
        }
        send_key(WXK_RETURN);
        prepare_canvas().render();
        prepare_canvas().render();
    }

    double first_instance_rotation_z() const
    {
        return m_plater->model().objects.front()->instances.front()->get_rotation().z();
    }

    void report_rotation(const char* what)
    {
        const GizmoObjectManipulation& manip = prepare_canvas().get_gizmos_manager().get_object_manipulation();
        const Vec3d model = m_plater->model().objects.front()->instances.front()->get_rotation();
        std::cerr << "HARNESS ANGLES " << what << " model=(" << Geometry::rad2deg(model.x()) << ", "
                  << Geometry::rad2deg(model.y()) << ", " << Geometry::rad2deg(model.z()) << ")"
                  << " relative=(" << manip.m_new_rotation.x() << ", " << manip.m_new_rotation.y() << ", "
                  << manip.m_new_rotation.z() << ")"
                  << " absolute=(" << manip.m_new_absolute_rotation.x() << ", " << manip.m_new_absolute_rotation.y()
                  << ", " << manip.m_new_absolute_rotation.z() << ")\n";
    }

    // A quarter turn about Y reads back as (180, 90, 180) rather than
    // (0, 90, 0): at ninety degrees on the middle axis, pulling Euler angles
    // back out of a transform is degenerate, and OrcaSlicer's extraction picks
    // that equivalent form. It is upstream's arithmetic -- driving the same
    // change straight through GizmoObjectManipulation gives the same triple --
    // and the fork matches it deliberately. What must stay true is the
    // orientation itself, which is what this checks; the numbers beside it are
    // only a different spelling of the same turn.
    void tool_strip_ninety_orientation()
    {
        type_into_field("##rotation_y", "90");
        wait_until_settled("ninety_typed_settled", [self = shared_from_this()] {
            self->report_rotation("after typing relative Y=90");
            const Transform3d actual =
                self->m_plater->model().objects.front()->instances.front()->get_transformation().get_rotation_matrix();
            const Transform3d expected = Transform3d(Eigen::AngleAxisd(M_PI / 2, Vec3d::UnitY()));
            const double difference = (actual.matrix() - expected.matrix()).cwiseAbs().maxCoeff();
            self->check(difference < 1e-9, "ninety_leaves_the_object_in_the_right_orientation");
            self->m_plater->undo();
            self->wait_until_settled("ninety_undo_settled", [self] { self->tool_strip_scale_step(); });
        });
    }

    // A typed rotation has to reach the model, and it has to reach it once:
    // the relative field says "turn it by this much", so 45 typed into Z is
    // 45 degrees, not 90, and the field goes back to zero afterwards.
    void tool_strip_typed_rotation()
    {
        const double before = first_instance_rotation_z();
        type_into_field("##rotation_z", "45");
        wait_until_settled("typed_rotation_settled", [self = shared_from_this(), before] {
            const double after = self->first_instance_rotation_z();
            const double turned = Geometry::rad2deg(after - before);
            std::cerr << "HARNESS ROTATION before=" << Geometry::rad2deg(before) << " after=" << Geometry::rad2deg(after)
                      << " turned=" << turned << '\n';
            self->check(std::abs(turned - 45.0) < 0.5, "typed_relative_rotation_turns_by_what_was_typed");
            self->capture_tool_strip("rotated");
            self->check(self->open_tool() == GLGizmosManager::Rotate, "typing_keeps_the_rotate_tool_open");
            self->tool_strip_absolute_rotation();
        });
    }

    // The absolute row says "put it at this angle", so 10 typed into Z with
    // the object already at 45 leaves it at 10, not 55. Then the reset beside
    // the relative row puts it back where the tool found it.
    void tool_strip_absolute_rotation()
    {
        type_into_field("##absolute_rotation_z", "10");
        wait_until_settled("typed_absolute_rotation_settled", [self = shared_from_this()] {
            const double absolute = Geometry::rad2deg(self->first_instance_rotation_z());
            std::cerr << "HARNESS ROTATION absolute=" << absolute << '\n';
            self->check(std::abs(absolute - 10.0) < 0.5, "typed_absolute_rotation_sets_the_angle");
            self->tool_strip_rotation_reset();
        });
    }

    void tool_strip_rotation_reset()
    {
        prepare_canvas().render();
        const ViewportToolStrip* strip = installed_shell()->prepare_canvas_presentation().tool_strip();
        const auto& rects = strip->values().field_rects();
        const bool has_reset = rects.count("##reset_rotate-ccw") == 1;
        check(has_reset, "rotation_reset_button_is_shown_once_rotated");
        if (!has_reset) {
            tool_strip_scale_step();
            return;
        }
        const StripRect& button = rects.at("##reset_rotate-ccw");
        const double scale = prepare_canvas().get_scale();
        const wxPoint at(int((button.x + button.w / 2) / scale), int((button.y + button.h / 2) / scale));
        send_canvas_mouse(wxEVT_MOTION, at, false);
        prepare_canvas().render();
        send_canvas_mouse(wxEVT_LEFT_DOWN, at, true);
        send_canvas_mouse(wxEVT_LEFT_UP, at, false);
        wait_until_settled("rotation_reset_settled", [self = shared_from_this()] {
            const double after = Geometry::rad2deg(self->first_instance_rotation_z());
            std::cerr << "HARNESS ROTATION after reset=" << after << '\n';
            self->check(std::abs(after) < 0.5, "reset_returns_the_rotation_to_where_the_tool_opened");
            self->tool_strip_ninety_orientation();
        });
    }

    void tool_strip_scale_step()
    {
        click_tool_strip(StripTool::Scale, "tool_strip_scale_click_settled", [self = shared_from_this()] {
            self->check(self->open_tool() == GLGizmosManager::Scale, "tool_strip_switches_rotate_to_scale");
            self->check_tool_panel_anchored(StripTool::Scale, "scale");
            self->capture_tool_strip("scale");
            self->click_tool_strip(StripTool::Scale, "tool_strip_second_scale_click_settled", [self] {
                self->check(self->open_tool() == GLGizmosManager::Undefined, "tool_strip_second_click_closes_scale");
                self->tool_strip_duplicate();
            });
        });
    }

    void tool_strip_duplicate()
    {
        const size_t before = first_object_instances();
        click_tool_strip(StripTool::Duplicate, "tool_strip_duplicate_click_settled", [self = shared_from_this(), before] {
            self->check(self->first_object_instances() == before + 1, "duplicate_adds_exactly_one_instance");
            self->capture_tool_strip("duplicated");
            self->m_plater->undo();
            self->wait_until_settled("tool_strip_duplicate_undo_settled", [self, before] {
                self->check(self->first_object_instances() == before, "one_undo_removes_the_duplicate");
                self->check(self->m_plater->select_object(0), "tool_strip_reselects_after_undo");
                self->tool_strip_more();
            });
        });
    }

    // More opens the canvas's own object menu. PopupMenu runs a modal loop,
    // so the check, the capture and the dismissal happen from a timer inside
    // it. Dismissing a native popup from inside needs EndMenu, so the step
    // runs on Windows only.
    void tool_strip_more()
    {
#ifdef _WIN32
        // Plater::PopupMenu shows its menus through the main frame, and a menu
        // names the window that popped it up for exactly as long as it is shown.
        // Taken once: MenuFactory::object_menu() appends items on every call.
        wxMenu* object_menu = m_plater->object_menu();
        auto dismissed      = std::make_shared<bool>(false);
        auto ticks          = std::make_shared<int>(0);
        auto shown_ticks    = std::make_shared<int>(0);
        auto* timer         = new wxTimer();
        timer->Bind(wxEVT_TIMER, [self = shared_from_this(), object_menu, timer, dismissed, ticks, shown_ticks](wxTimerEvent&) {
            const bool shown = object_menu->GetInvokingWindow() == self->m_frame;
            // Not up yet, or up but not yet painted: the timer runs every
            // 100 ms for up to five seconds, and waits three ticks once the
            // menu is up.
            if (shown ? ++*shown_ticks < 3 : ++*ticks < 50)
                return;
            self->check(shown, "more_opens_the_object_menu");
            if (shown) {
                const bool dark = self->m_state->dark_appearance.value_or(false);
                self->blit_screen(self->m_frame->GetScreenRect(), std::string("tool-strip-more-") + (dark ? "dark" : "light"));
                ::EndMenu();
            }
            *dismissed = true;
            timer->Stop();
            wxTheApp->CallAfter([timer] { delete timer; });
        });
        timer->Start(100);
        press_tool_strip(StripTool::More);
        wait_until([dismissed] { return *dismissed; }, "tool_strip_more_menu_dismissed",
                   [self = shared_from_this()] { self->tool_strip_orbit(); });
#else
        tool_strip_orbit();
#endif
    }

    void tool_strip_orbit()
    {
        prepare_canvas().select_view("left");
        wait_until_settled("tool_strip_orbit_settled", [self = shared_from_this()] {
            self->capture_tool_strip("orbited");
            self->tool_strip_narrow();
        });
    }

    void tool_strip_narrow()
    {
        m_frame->Maximize(false);
        m_frame->SetSize(m_frame->FromDIP(wxSize(1000, 700)));
        wait_until_settled("tool_strip_narrow_settled", [self = shared_from_this()] {
            const wxRect canvas = self->prepare_canvas().get_wxglcanvas()->GetScreenRect();
            self->check(canvas.GetRight() < installed_shell()->agent_pane()->GetScreenRect().GetLeft(),
                        "narrow_canvas_ends_before_the_agent_pane");
            const StripLayout narrow = self->tool_strip_layout();
            self->check(narrow.strip.x >= 0.f &&
                            narrow.strip.x + narrow.strip.w <= float(self->prepare_canvas().get_canvas_size().get_width()),
                        "strip_row_fits_the_narrow_canvas");
            self->prepare_canvas().render();
            const bool dark = self->m_state->dark_appearance.value_or(false);
            self->write_screen_capture(self->m_frame->GetScreenRect(), std::string("tool-strip-narrow-") + (dark ? "dark" : "light"));
            self->finish();
        });
    }

    void write_screen_capture(const wxRect& rect, const std::string& name)
    {
        fs::create_directories(m_state->capture_dir);
        // A screen blit photographs whatever covers the frame, and another
        // application's window on the same desktop is enough to ruin it. Hold
        // the frame on top for the capture, and give WebView2 time to repaint
        // the area it gets back.
        const long style = m_frame->GetWindowStyleFlag();
        m_frame->SetWindowStyleFlag(style | wxSTAY_ON_TOP);
        m_frame->Raise();
        for (int settle = 0; settle < 10; ++settle) {
            wxYield();
            wxMilliSleep(50);
        }
        struct RestoreStyle { wxFrame* frame; long style; ~RestoreStyle() { frame->SetWindowStyleFlag(style); } }
            restore{m_frame, style};
        blit_screen(rect, name);
    }

    // The capture itself, with no yield: safe inside a popup menu's modal loop.
    // The desktop scaling this process does not see.
    //
    // The application is DPI-unaware, so its own coordinates are the
    // virtualized desktop's -- 1728x1084 on a 3456x2168 screen at 200%. The
    // screen DC blits real pixels, so a rectangle has to be scaled up before
    // it names the same place. The ratio is only discoverable through the
    // device itself: HORZRES is what this process is told, DESKTOPHORZRES is
    // what is really there. It is 1 whenever the session runs unscaled, which
    // is why captures were right until the session reconnected at 200%.
    double desktop_scale() const
    {
#ifdef _WIN32
        HDC hdc = ::GetDC(nullptr);
        const int physical = ::GetDeviceCaps(hdc, DESKTOPHORZRES);
        const int logical  = ::GetDeviceCaps(hdc, HORZRES);
        ::ReleaseDC(nullptr, hdc);
        if (logical > 0 && physical > 0)
            return double(physical) / double(logical);
#endif
        return 1.0;
    }

    void blit_screen(const wxRect& logical, const std::string& name)
    {
        fs::create_directories(m_state->capture_dir);
        const double scale = desktop_scale();
        const wxRect rect(int(logical.x * scale), int(logical.y * scale), int(logical.width * scale),
                          int(logical.height * scale));
        wxScreenDC screen;
        wxBitmap   bitmap(rect.GetWidth(), rect.GetHeight());
        {
            wxMemoryDC memory(bitmap);
            memory.Blit(0, 0, rect.GetWidth(), rect.GetHeight(), &screen, rect.GetLeft(), rect.GetTop());
        }
        const std::string file = (m_state->capture_dir / (name + ".png")).string();
        const bool ok = bitmap.IsOk() && bitmap.ConvertToImage().SaveFile(wxString::FromUTF8(file), wxBITMAP_TYPE_PNG);
        check(ok, "captured_" + name);
        if (ok) std::cout << "HARNESS ARTIFACT " << name << " " << file
                          << " " << rect.GetWidth() << "x" << rect.GetHeight() << std::endl;
    }

    // Phase 2: the packaged React page in the real WKWebView completes the
    // versioned handshake, a scripted user message round-trips through the
    // real script-message channel and streams to completion, native selection
    // changes push context over the live bridge, and a reload reconstructs
    // the page from native state.
    void verify_agent_bridge()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        check(web_view.webview() != nullptr, "agent_webview_created");
        wait_until([&web_view] { return web_view.host().handshake_complete(); }, "agent_bridge_handshake",
                   [self = shared_from_this()] { self->agent_send_scripted_message(); });
    }

    void verify_live_agent()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        check(web_view.webview() != nullptr, "live_agent_webview_created");
        wait_until([&web_view] { return web_view.host().handshake_complete(); }, "live_agent_bridge_handshake",
                   [self = shared_from_this()] { self->live_agent_context_and_attachment(); });
    }

    void verify_unavailable_agent()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        wait_until([&web_view] { return web_view.host().handshake_complete(); },
                   "unconfigured_agent_bridge_handshake",
                   [self = shared_from_this()] { self->verify_not_set_up_empty_state(); });
    }

    // What the packaged page actually renders in the dock before anything is
    // delegated: the offer replaces the conversation chrome, its one action
    // opens setup, and the ask box stays put and disabled.
    // The page reports the answer back over the bridge's draft message, which
    // the host stores where this harness can read it.
    void verify_not_set_up_empty_state()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        check(web_view.host().availability() == Agent::AgentAvailability::Unavailable,
              "missing_cloud_consent_is_unavailable");
        WebView::RunScript(
            web_view.webview(),
            "(function () {"
            "  var q = function (s) { return document.querySelector(s); };"
            "  var setup = q('[data-testid=\"agent-not-configured\"] button.primary');"
            "  var ask = q('.composer textarea');"
            "  window.__jusprinTest && window.__jusprinTest.setDraft(["
            "    q('[data-testid=\"agent-not-configured\"]') ? 'offer' : 'no-offer',"
            "    q('[data-testid=\"agent-not-configured-header\"]') ? 'header' : 'no-header',"
            "    q('[data-testid=\"context-summary\"]') ? 'chrome' : 'no-chrome',"
            "    setup && setup.disabled ? 'setup-inert' : 'setup-live',"
            "    ask && ask.disabled ? 'ask-inert' : 'ask-live'"
            "  ].join('|'));"
            "})()");
        wait_until([self = shared_from_this()] { return !self->persistence().draft().empty(); },
                   "not_set_up_state_reported", [self = shared_from_this()] {
                       self->check(self->persistence().draft() ==
                                       "offer|header|no-chrome|setup-live|ask-inert",
                                   "not_set_up_dock_is_one_live_offer");
                       if (self->persistence().draft() != "offer|header|no-chrome|setup-live|ask-inert")
                           std::cerr << "HARNESS DETAIL dock state was " << self->persistence().draft() << '\n';
                       self->verify_setup_opens_the_chooser();
                   });
    }

    // The setup flow in the real WKWebView. Each step clicks, then reports
    // from a timeout so React has re-rendered before the page is asked what
    // it now shows; chaining clicks inside one script would read the previous
    // screen.
    void verify_setup_opens_the_chooser()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        persistence().set_draft({});
        WebView::RunScript(web_view.webview(),
                           "(function () {"
                           "  document.querySelector('[data-testid=\"agent-not-configured\"] button.primary').click();"
                           "  setTimeout(function () {"
                           "    window.__jusprinTest.setDraft("
                           "      document.querySelector('[data-testid=\"setup-chooser\"]') ? 'chooser' : 'no-chooser');"
                           "  }, 0);"
                           "})()");
        wait_until([self = shared_from_this()] { return !self->persistence().draft().empty(); },
                   "setup_chooser_reported", [self = shared_from_this()] {
                       self->check(self->persistence().draft() == "chooser", "offer_action_opens_setup");
                       self->verify_setup_reaches_the_key_screen();
                   });
    }

    void verify_setup_reaches_the_key_screen()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        persistence().set_draft({});
        WebView::RunScript(
            web_view.webview(),
            "(function () {"
            "  document.querySelector('[data-testid=\"setup-row-api-key\"]').click();"
            "  setTimeout(function () {"
            "    var screen = document.querySelector('[data-testid=\"setup-api-key\"]');"
            "    var live = [];"
            "    document.querySelectorAll('[data-testid=\"setup-api-key\"] .setup-tab').forEach("
            "      function (tab) { if (!tab.disabled) live.push(tab.textContent); });"
            "    window.__jusprinTest.setDraft("
            "      (screen ? 'key-screen' : 'no-key-screen') + '|' + live.join(','));"
            "  }, 0);"
            "})()");
        wait_until([self = shared_from_this()] { return !self->persistence().draft().empty(); },
                   "setup_key_screen_reported", [self = shared_from_this()] {
                       self->check(self->persistence().draft() == "key-screen|OpenAI",
                                   "key_screen_offers_only_verifiable_providers");
                       if (self->persistence().draft() != "key-screen|OpenAI")
                           std::cerr << "HARNESS DETAIL key screen was " << self->persistence().draft() << '\n';
                       self->verify_setup_key_check_round_trips();
                   });
    }

    // A key check driven from the page, over the real script-message channel,
    // through the real host and HTTP transport. JUSPRIN_OPENAI_ENDPOINT points
    // at a closed local port for this run, so the whole path is exercised and
    // the failure the user sees is a real one, with no external service
    // involved.
    void verify_setup_key_check_round_trips()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        persistence().set_draft({});
        WebView::RunScript(web_view.webview(),
                           "window.__jusprinTest.checkKey('openai', 'sk-harness-not-a-real-key')");
        wait_until([self = shared_from_this()] { return self->persistence().draft() == "setup-error"; },
                   "setup_key_check_reports_failure",
                   [self = shared_from_this()] {
                       // An unreachable provider must leave nothing configured.
                       self->check(installed_shell()->agent_pane()->web_view().host().availability() ==
                                       Agent::AgentAvailability::Unavailable,
                                   "unreachable_provider_does_not_configure_the_agent");
                       self->unconfigured_agent_refuses_to_answer();
                   },
                   [self = shared_from_this()] { self->poll_setup_error(); });
    }

    void poll_setup_error()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        WebView::RunScript(web_view.webview(),
                           "(function () {"
                           "  if (document.querySelector('[data-testid=\"setup-error\"]'))"
                           "    window.__jusprinTest.setDraft('setup-error');"
                           "})()");
    }

    void unconfigured_agent_refuses_to_answer()
    {
        auto           self         = shared_from_this();
        AgentWebView&  web_view     = installed_shell()->agent_pane()->web_view();
        const std::size_t objects_before = m_plater->model().objects.size();
        WebView::RunScript(web_view.webview(),
                           "window.__jusprinTest && window.__jusprinTest.send('do not use the mock')");
        wait_until(
            [&web_view] {
                const auto conversation = web_view.host().conversation();
                return conversation.size() >= 2 && conversation.back().state == Agent::MessageState::Failed;
            },
            "unconfigured_agent_request_fails_visibly", [self, objects_before] {
                const auto conversation = installed_shell()->agent_pane()->web_view().host().conversation();
                self->check(conversation.back().error && conversation.back().error->code == "agent_unavailable",
                            "unconfigured_agent_does_not_fall_back_to_mock");
                self->check(self->m_plater->model().objects.size() == objects_before &&
                                self->m_plater->select_object(1),
                            "unconfigured_agent_keeps_orca_usable");
                self->check(self->m_plater->new_project(true, true) != wxID_CANCEL,
                            "unconfigured_agent_teardown_project");
                self->finish();
            });
    }

    void live_agent_context_and_attachment()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        check(web_view.host().availability() == Agent::AgentAvailability::Ready, "live_agent_service_ready");
        check(m_plater->select_object(0), "live_agent_target_selected");
        m_objects_before_tool = m_plater->model().objects.size();
        m_live_copies_before  = copy_count();
        const std::size_t attachments_before = persistence().document().attachments().size();
        WebView::RunScript(
            web_view.webview(),
            "window.__jusprinTest && window.__jusprinTest.attach('live-context.txt', "
            "'SnVzUHJpbiBsaXZlIHZlcmlmaWNhdGlvbiBwaHJhc2U6IGNvYmFsdCBuYXJ3aGFsLg==', 'text/plain')");
        wait_until(
            [this, attachments_before] { return persistence().document().attachments().size() > attachments_before; },
            "live_agent_attachment_staged", [self = shared_from_this()] { self->live_agent_send_context_question(); });
    }

    void live_agent_send_context_question()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        const std::size_t messages_before = web_view.host().conversation().size();
        WebView::RunScript(
            web_view.webview(),
            "window.__jusprinTest && window.__jusprinTest.send('Read the attached note and the authoritative workspace. "
            "Reply exactly: COBALT NARWHAL | 2 plates | selected cube-a. Do not call a tool.')");
        wait_until(
            [&web_view, messages_before] { return web_view.host().conversation().size() >= messages_before + 2; },
            "live_agent_context_request_started", [self = shared_from_this()] {
                AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
                web_view.reload();
                self->wait_until([&web_view] { return web_view.host().handshake_complete(); },
                                 "live_agent_reload_during_stream", [self] { self->live_agent_verify_context(); });
            });
    }

    void live_agent_verify_context()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        wait_until(
            [&web_view] {
                const auto conversation = web_view.host().conversation();
                return !conversation.empty() && conversation.back().role == Agent::MessageRole::Assistant &&
                       (conversation.back().state == Agent::MessageState::Complete ||
                        conversation.back().state == Agent::MessageState::Failed);
            },
            "live_agent_context_reply_terminal", [self = shared_from_this()] {
                const auto conversation = installed_shell()->agent_pane()->web_view().host().conversation();
                if (conversation.back().state != Agent::MessageState::Complete) {
                    const std::string code = conversation.back().error ? conversation.back().error->code : "unknown";
                    self->fail("live Agent context reply failed: " + code);
                    return;
                }
                std::string reply = conversation.back().text;
                std::transform(reply.begin(), reply.end(), reply.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                self->check(reply.find("cobalt narwhal") != std::string::npos,
                            "live_agent_used_decoded_attachment");
                self->check(reply.find("2 plates") != std::string::npos && reply.find("cube-a") != std::string::npos,
                            "live_agent_used_native_workspace_context");
                self->live_agent_send_mutation();
            });
    }

    void live_agent_send_mutation()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        const std::size_t activities_before = web_view.host().tools().activities().size();
        WebView::RunScript(
            web_view.webview(),
            "window.__jusprinTest && window.__jusprinTest.send('Make one more copy of the currently selected object now. Use the "
            "plate_layout tool with the exact sessionId and objectId from the authoritative workspace context and quantity 2.');");
        wait_until(
            [&web_view, activities_before] {
                const auto& activities = web_view.host().tools().activities();
                return activities.size() > activities_before && activities.back().state == Agent::ToolState::Pending;
            },
            "live_agent_tool_proposal_pending", [self = shared_from_this()] { self->live_agent_decide(); });
    }

    void live_agent_decide()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        const auto& activity = web_view.host().tools().activities().back();
        check(activity.tool == "plate_layout", "live_agent_proposed_typed_duplicate");
        check(activity.requires_approval, "live_agent_cannot_bypass_native_approval");
        m_live_action_id = activity.action_id;
        if (!m_live_rejection_done) {
            WebView::RunScript(web_view.webview(),
                               wxString::FromUTF8("window.__jusprinTest && window.__jusprinTest.decide('" +
                                                  m_live_action_id + "', 'reject')"));
            wait_until(
                [&web_view, this] {
                    const Agent::ToolActivity* current = web_view.host().tools().find(m_live_action_id);
                    const auto conversation = web_view.host().conversation();
                    return current != nullptr && current->state == Agent::ToolState::Rejected &&
                           !conversation.empty() && conversation.back().role == Agent::MessageRole::Assistant &&
                           (conversation.back().state == Agent::MessageState::Complete ||
                            conversation.back().state == Agent::MessageState::Failed);
                },
                "live_agent_rejection_and_followup_terminal", [self = shared_from_this()] {
                    const auto conversation = installed_shell()->agent_pane()->web_view().host().conversation();
                    if (conversation.back().state != Agent::MessageState::Complete) {
                        const std::string code = conversation.back().error ? conversation.back().error->code : "unknown";
                        self->fail("live Agent rejection follow-up failed: " + code);
                        return;
                    }
                    self->check(self->copy_count() == self->m_live_copies_before,
                                "live_agent_rejection_changes_nothing");
                    self->m_live_rejection_done = true;
                    self->live_agent_send_mutation();
                });
            return;
        }
        WebView::RunScript(web_view.webview(),
                           wxString::FromUTF8("window.__jusprinTest && window.__jusprinTest.decide('" + m_live_action_id +
                                              "', 'approve')"));
        wait_until(
            [&web_view, this] {
                const Agent::ToolActivity* current = web_view.host().tools().find(m_live_action_id);
                const auto conversation = web_view.host().conversation();
                return current != nullptr && current->state == Agent::ToolState::Succeeded &&
                       !conversation.empty() && conversation.back().role == Agent::MessageRole::Assistant &&
                       (conversation.back().state == Agent::MessageState::Complete ||
                        conversation.back().state == Agent::MessageState::Failed);
            },
            "live_agent_native_result_and_followup_terminal", [self = shared_from_this()] {
                const auto conversation = installed_shell()->agent_pane()->web_view().host().conversation();
                if (conversation.back().state != Agent::MessageState::Complete) {
                    const std::string code = conversation.back().error ? conversation.back().error->code : "unknown";
                    self->fail("live Agent follow-up failed: " + code);
                    return;
                }
                self->check(true, "live_agent_native_result_and_followup_complete");
                self->live_agent_verify();
            });
    }

    void live_agent_verify()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        check(copy_count() == m_live_copies_before + 1, "live_agent_mutated_real_orca_model_once");
        check(m_plater->can_undo_project(), "live_agent_mutation_is_in_orca_history");
        const auto conversation = web_view.host().conversation();
        check(conversation.size() >= 3 && !conversation.back().text.empty(), "live_agent_explains_structured_native_result");
        std::size_t matching_actions = 0;
        for (const auto& action : web_view.host().tools().activities())
            if (action.action_id == m_live_action_id)
                ++matching_actions;
        check(matching_actions == 1, "live_agent_action_id_is_idempotent");
        const auto selected = installed_shell()->workspace()->snapshot().plates[0].objects[0].id;
        const auto selection = installed_shell()->workspace()->select_object(selected);
        check(selection.succeeded() || selection.error == Workspace::WorkspaceError::NoChange, "live_agent_native_selection");
        const auto inspect_id = web_view.host().tools().propose({"workspace_inspect", "{}"}, "live-selection-proof").action_id;
        web_view.host().pump_tools();
        const auto* inspected = web_view.host().tools().find(inspect_id);
        check(inspected && inspected->state == Agent::ToolState::Succeeded &&
              nlohmann::json::parse(inspected->result_json)["selection"]["items"].size() == 1, "live_agent_shared_selection_result");
        check(installed_shell()->workspace()->undo().succeeded(), "live_agent_native_undo_executes");
        check(copy_count() == m_live_copies_before, "live_agent_native_undo_restores_object_count");
        m_settings_original = nlohmann::json::object();
        for (const auto& item : installed_shell()->workspace()->read_settings({"layer_height", "sparse_infill_density"}).items)
            m_settings_original[item.key] = item.value;
        m_settings_patch = {{"layer_height", "0.16"}, {"sparse_infill_density", "25%"}};
        live_agent_settings(0);
    }

    void live_agent_settings(int stage)
    {
        auto& view = installed_shell()->agent_pane()->web_view();
        const auto first_activity = view.host().tools().activities().size();
        const auto first_message = view.host().conversation().size();
        const auto changes = stage == 2 ? m_settings_original : m_settings_patch;
        const std::string prompt = "Test process settings using all four settings tools. First settings_search for infill, "
            "then settings_get for layer_height and sparse_infill_density, then settings_preview_patch with changes=" +
            changes.dump() + ". If valid, call settings_apply_patch with those same changes and the session/revision from "
            "that preview. Wait for approval in JusPrin. After the terminal result, explain it briefly and stop. "
            "Do not retry a rejected call or use other mutation tools.";
        WebView::RunScript(view.webview(), wxString::FromUTF8("window.__jusprinTest.send(" + nlohmann::json(prompt).dump() + ")"));
        wait_until([first_activity, first_message] {
            const auto& host = installed_shell()->agent_pane()->web_view().host();
            const auto& activities = host.tools().activities();
            const auto messages = host.conversation();
            return (activities.size() > first_activity && activities.back().requires_approval &&
                    activities.back().state == Agent::ToolState::Pending) ||
                (messages.size() > first_message && messages.back().role == Agent::MessageRole::Assistant &&
                 (messages.back().state == Agent::MessageState::Complete || messages.back().state == Agent::MessageState::Failed));
        }, "live_settings_proposal_or_terminal", [self = shared_from_this(), stage, first_activity] {
            auto& view = installed_shell()->agent_pane()->web_view();
            const auto& activities = view.host().tools().activities();
            if (activities.size() <= first_activity || activities.back().state != Agent::ToolState::Pending ||
                activities.back().tool != "settings_apply_patch") {
                self->fail("Live settings turn did not produce the expected approval proposal");
                return;
            }
            for (const auto* name : {"settings_search", "settings_get", "settings_preview_patch"})
                self->check(std::any_of(activities.begin() + first_activity, activities.end(), [name](const auto& action) {
                    return action.tool == name && action.state == Agent::ToolState::Succeeded;
                }), std::string("live_agent_called_") + name);
            const std::string id = activities.back().action_id;
            self->check(activities.back().requires_approval, "live_settings_requires_approval");
            // Measure the decision against the state the proposal actually
            // confirmed, after the model's read/preview requests have finished.
            const auto before = installed_shell()->workspace()->snapshot();
            const auto arguments = nlohmann::json::parse(activities.back().arguments_json);
            self->check(arguments["expectedRevision"] == before.revision, "live_settings_proposal_revision_current");
            self->wait_until([id] {
                const auto& host = installed_shell()->agent_pane()->web_view().host();
                const auto* action = host.tools().find(id);
                const auto messages = host.conversation();
                return action && Agent::tool_state_terminal(action->state) && !messages.empty() &&
                    messages.back().role == Agent::MessageRole::Assistant &&
                    (messages.back().state == Agent::MessageState::Complete || messages.back().state == Agent::MessageState::Failed);
            }, "live_settings_result_and_followup", [self, stage, id, before] {
                const auto& host = installed_shell()->agent_pane()->web_view().host();
                const auto* action = host.tools().find(id);
                self->check(host.conversation().back().state == Agent::MessageState::Complete, "live_settings_followup_complete");
                const auto after = installed_shell()->workspace()->snapshot();
                self->check(after.can_undo == before.can_undo, "live_settings_preserves_project_undo");
                if (stage == 0) {
                    self->check(action->state == Agent::ToolState::Rejected, "live_settings_rejected");
                    self->check(after.revision == before.revision, "live_settings_rejection_preserves_revision");
                } else {
                    self->check(action->state == Agent::ToolState::Succeeded, "live_settings_applied");
                    if (action->state != Agent::ToolState::Succeeded) { self->fail("Live settings apply failed"); return; }
                    const auto result = nlohmann::json::parse(action->result_json);
                    self->check(result["applied"] == true && result["changes"].size() == 2 && result["projectUndo"] == false,
                                "live_settings_structured_batch_result");
                    self->check(result["processPresetDirty"] == (stage == 1), "live_settings_dirty_and_inverse");
                }
                const auto expected = stage == 1 ? self->m_settings_patch : self->m_settings_original;
                for (const auto& item : installed_shell()->workspace()->read_settings({"layer_height", "sparse_infill_density"}).items)
                    self->check(item.value == expected[item.key], "live_settings_native_value_" + item.key);
                if (stage < 2) self->live_agent_settings(stage + 1);
                else self->live_agent_plan();
            }, [id, stage] { click_rendered_tool_decision(id, stage != 0); });
        });
    }

    // Two patches in one plan: both answer queued, the turn ends, one click on
    // the plan card runs both in order, and the later patch is not stale
    // after the earlier one's change.
    void live_agent_plan()
    {
        auto& view = installed_shell()->agent_pane()->web_view();
        const auto first_activity = view.host().tools().activities().size();
        const std::string prompt = "I want to approve two settings changes on one approval card. Call settings_preview_patch then "
            "settings_apply_patch with changes={\"layer_height\": \"0.16\"} and planId \"live-plan\"; then settings_preview_patch then "
            "settings_apply_patch with changes={\"sparse_infill_density\": \"25%\"} and the same planId. Then stop and ask me to approve.";
        WebView::RunScript(view.webview(), wxString::FromUTF8("window.__jusprinTest.send(" + nlohmann::json(prompt).dump() + ")"));
        wait_until([first_activity] {
            const auto& host = installed_shell()->agent_pane()->web_view().host();
            const auto messages = host.conversation();
            return host.tools().activities().size() > first_activity && !messages.empty() &&
                   messages.back().role == Agent::MessageRole::Assistant &&
                   (messages.back().state == Agent::MessageState::Complete || messages.back().state == Agent::MessageState::Failed);
        }, "live_plan_turn_ended", [self = shared_from_this(), first_activity] {
            const auto& host = installed_shell()->agent_pane()->web_view().host();
            std::vector<std::string> members;
            for (auto it = host.tools().activities().begin() + first_activity; it != host.tools().activities().end(); ++it)
                if (it->plan_id == "live-plan" && it->state == Agent::ToolState::Pending) members.push_back(it->action_id);
            self->check(members.size() == 2, "live_plan_two_members_waiting");
            if (members.size() != 2) { self->fail("Live plan turn did not queue two patches"); return; }
            const std::string script = "(() => { const b = document.querySelector('[data-testid=\"plan-live-plan\"] button.primary');"
                                       " if (b && !b.disabled) b.click(); })()";
            WebView::RunScript(installed_shell()->agent_pane()->web_view().webview(), wxString::FromUTF8(script));
            self->wait_until([members] {
                const auto& host = installed_shell()->agent_pane()->web_view().host();
                return std::all_of(members.begin(), members.end(), [&host](const std::string& id) {
                    const auto* action = host.tools().find(id);
                    return action && Agent::tool_state_terminal(action->state);
                });
            }, "live_plan_members_terminal", [self, members] {
                const auto& host = installed_shell()->agent_pane()->web_view().host();
                for (const auto& id : members)
                    self->check(host.tools().find(id)->state == Agent::ToolState::Succeeded, "live_plan_member_succeeded");
                for (const auto& item : installed_shell()->workspace()->read_settings({"layer_height", "sparse_infill_density"}).items)
                    self->check(item.value == self->m_settings_patch[item.key], "live_plan_native_value_" + item.key);
                Workspace::SettingsPatch inverse;
                for (const auto& [key, value] : self->m_settings_original.items()) inverse.changes[key] = value.get<std::string>();
                Workspace::SettingsPreview applied;
                auto* workspace = installed_shell()->workspace();
                self->check(workspace->apply_settings(inverse, Workspace::settings_confirmation(workspace->preview_settings(inverse)), applied)
                                .succeeded(), "live_plan_restored");
                self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "live_agent_teardown_project");
                self->finish();
            });
        });
    }

    static void click_rendered_tool_decision(const std::string& id, bool approve)
    {
        const std::string script = "(() => { const b = Array.from(document.querySelectorAll('[data-testid=\"tool-" + id +
            "\"] button')).find(b => b.textContent.trim() === '" + (approve ? "Approve" : "Reject") +
            "'); if (b && b.getClientRects().length && !b.disabled) b.click(); })()";
        WebView::RunScript(installed_shell()->agent_pane()->web_view().webview(), wxString::FromUTF8(script));
    }

    void mcp_wait(std::function<void()> next)
    {
        wait_until([this] { return m_mcp_client->done(); }, "mcp_response_complete", std::move(next));
    }

    void mcp_request(nlohmann::json request)
    {
        if (!m_mcp_client) m_mcp_client = std::make_unique<JusPrinTest::NativeMcpClient>(m_state->bridge);
        m_mcp_client->request(installed_shell()->agent_pane()->web_view().host().mcp()->server(), std::move(request));
    }

    nlohmann::json mcp_result() const
    {
        const auto messages = m_mcp_client->messages();
        if (messages.empty() || !messages.back().contains("result")) throw std::runtime_error("No MCP tool result");
        return messages.back()["result"];
    }

    // Both MCP modes hand the Agent pane to a real client or a person, so
    // the page has to be up before the fixture is worth preparing.
    void prepare_mcp_slice(std::function<void()> next)
    {
        wait_for_agent_page("mcp_fixture", [self = shared_from_this(), next = std::move(next)]() mutable {
            self->slice_mcp_fixture(std::move(next));
        });
    }

    void slice_mcp_fixture(std::function<void()> next)
    {
        installed_shell()->status_row()->request_slice();
        wait_until([this] {
            const auto* plate = m_plater->get_partplate_list().get_curr_plate();
            return plate && plate->is_slice_result_valid() && !m_plater->is_background_process_slicing();
        }, "mcp_fixture_has_real_slice", [self = shared_from_this(), next] {
            installed_shell()->status_row()->request_prepare();
            // The tab has to be named, not merely "not Preview": request_prepare
            // asks the notebook to select Prepare, and Notebook::SetSelection
            // abandons a vetoed change without saying so. Home satisfies "not
            // Preview" too, so a check written that way passes whether the
            // navigation happened or never ran -- and Home is where the fixture
            // was in fact being left, with the whole workspace status row (plate
            // label, review panel, return button) hidden because that row only
            // shows on Prepare or Preview.
            self->wait_until([self] {
                return self->m_notebook->GetSelection() == MainFrame::tp3DEditor && !self->m_plater->is_preview_shown();
            }, "mcp_fixture_prepare", next);
        });
    }

    void begin_mcp()
    {
        auto& view = installed_shell()->agent_pane()->web_view();
        check(view.host().mcp() != nullptr, "mcp_started_without_launch_option");
        const auto discovery = Mcp::read_discovery(view.host().mcp()->discovery_path());
        check(discovery.has_value(), "mcp_discovery_file_published");
        check(discovery && discovery->url == view.host().mcp()->server().url(), "mcp_discovery_file_matches_listener");
        check(discovery && discovery->app_version == Mcp::mcp_build_version(), "mcp_discovery_file_build_version");
        if (m_state->mcp_bridge)
            m_state->bridge = std::make_shared<JusPrinTest::StdioClient>(JUSPRIN_MCP_BRIDGE_PATH,
                                                                       view.host().mcp()->discovery_path().u8string());
        m_objects_before_tool = m_plater->model().objects.size();
        mcp_request(JusPrinTest::request("server/discover"));
        mcp_wait([self = shared_from_this()] {
            self->check(self->mcp_result()["supportedVersions"] == nlohmann::json::array({Mcp::kProtocolVersion}), "mcp_real_discovery");
            self->check(self->mcp_result()["ttlMs"] == 0 && self->mcp_result()["cacheScope"] == "private",
                        "mcp_real_discovery_cache_policy");
            self->mcp_request(JusPrinTest::request("tools/list"));
            self->mcp_wait([self] {
                const auto result = self->mcp_result();
                if (self->m_state->mcp_bridge)
                    self->check(!result.contains("ttlMs") && !result.contains("cacheScope") && !result.contains("resultType"),
                                "mcp_legacy_catalog_omits_modern_cache_fields");
                else
                    self->check(result["ttlMs"] == 0 && result["cacheScope"] == "private", "mcp_real_catalog_cache_policy");
                const auto tools = result["tools"];
                // The live catalog is the registry's MCP-exposed list, in its
                // deterministic order, and nothing else: a tool that forgets
                // its exposure shows up here as a name that moved. A page
                // holds 25; the rest follows the cursor.
                const auto exposed = Agent::ToolRegistry::instance().exposed(Agent::ToolExposure::Mcp);
                bool same = tools.size() == std::min<std::size_t>(25, exposed.size()) &&
                            result.contains("nextCursor") == (exposed.size() > 25);
                for (std::size_t index = 0; same && index < tools.size(); ++index)
                    same = tools[index]["name"] == exposed[index].get().name;
                self->check(same, "mcp_real_registry_catalog");
                self->mcp_request(JusPrinTest::request("tools/call", {{"name", "workspace_inspect"}}));
                self->mcp_wait([self] {
                    const auto result = self->mcp_result()["structuredContent"];
                    self->check(result["plateCount"] == 2 && result["objectCount"] == self->m_objects_before_tool, "mcp_real_workspace_snapshot");
                    self->mcp_settings_reads();
                });
            });
        });
    }

    void mcp_settings_reads()
    {
        mcp_request(JusPrinTest::request("tools/call", {{"name", "settings_search"}, {"arguments", {{"query", "infill"}}}}));
        mcp_wait([self = shared_from_this()] {
            self->check(!self->mcp_result()["structuredContent"]["items"].empty(), "mcp_settings_search_results");
            self->mcp_request(JusPrinTest::request("tools/call", {{"name", "settings_get"},
                {"arguments", {{"keys", {"layer_height", "sparse_infill_density"}}}}}));
            self->mcp_wait([self] {
                const auto result = self->mcp_result()["structuredContent"];
                self->m_settings_original = nlohmann::json::object();
                for (const auto& item : result["items"])
                    self->m_settings_original[item["key"].get<std::string>()] = item["value"];
                self->check(self->m_settings_original.size() == 2, "mcp_settings_get_two_values");
                self->m_settings_patch = {{"layer_height", "0.16"}, {"sparse_infill_density", "25%"}};
                self->mcp_request(JusPrinTest::request("tools/call", {{"name", "settings_preview_patch"},
                    {"arguments", {{"changes", self->m_settings_patch}}}}));
                self->mcp_wait([self] {
                    const auto preview = self->mcp_result()["structuredContent"];
                    self->check(preview["valid"] == true && preview["changes"].size() == 2, "mcp_settings_preview_two_changes");
                    self->mcp_mutation(0);
                });
            });
        });
    }

    void mcp_mutation(int scenario)
    {
        const auto snapshot = installed_shell()->workspace()->snapshot();
        mcp_request(JusPrinTest::request("tools/call", {{"name", "settings_apply_patch"},
            {"arguments", {{"expectedSessionId", std::to_string(snapshot.session.value())}, {"expectedRevision", snapshot.revision},
                           {"changes", scenario == 2 ? m_settings_original : m_settings_patch}}}}));
        wait_until([this] {
            m_mcp_client->poll();
            const auto& activities = installed_shell()->agent_pane()->web_view().host().tools().activities();
            return !activities.empty() && activities.back().tool == "settings_apply_patch" &&
                   activities.back().state == Agent::ToolState::Pending && m_mcp_client->streaming();
        }, "mcp_native_approval_pending", [self = shared_from_this(), scenario] {
            auto& view = installed_shell()->agent_pane()->web_view();
            const auto id = view.host().tools().activities().back().action_id;
            self->check(self->m_mcp_client->streaming(), "mcp_settings_progress_stream");
            self->check(self->m_plater->model().objects.size() == self->m_objects_before_tool, "mcp_settings_preserve_objects");
            if (scenario == 3) {
                auto* tab = self->m_app.get_tab(Preset::TYPE_PRINT);
                tab->activate_option("wall_loops", "Strength");
                auto* field = tab->get_field("wall_loops");
                if (!field) throw std::runtime_error("Missing wall_loops field");
                field->set_value(boost::any(5), false);
                field->field_changed();
                self->mcp_verify_mutation(scenario);
            } else if (scenario == 4) {
                self->m_mcp_client->close();
                self->wait_until([id] {
                    const auto* activity = installed_shell()->agent_pane()->web_view().host().tools().find(id);
                    return activity && activity->state == Agent::ToolState::Cancelled;
                }, "mcp_disconnect_cancels_native_proposal", [self] { self->mcp_teardown(); });
            } else if (scenario == 1) {
                view.reload();
                self->wait_until([] { return installed_shell()->agent_pane()->web_view().host().handshake_complete(); },
                    "mcp_webview_reload_reconnects", [self, scenario, id] { self->mcp_click_decision(scenario, id); });
            } else self->mcp_click_decision(scenario, id);
        });
    }

    void mcp_click_decision(int scenario, const std::string& id)
    {
        // Exercise the rendered button, not the decision hook: MCP requests
        // have no chat message, and a hidden/missing card must fail this test.
        wait_until([id] {
            const auto* activity = installed_shell()->agent_pane()->web_view().host().tools().find(id);
            return activity && activity->state != Agent::ToolState::Pending;
        }, "mcp_visible_approval_button_activated", [self = shared_from_this(), scenario] {
            self->mcp_verify_mutation(scenario);
        }, [scenario, id] {
            click_rendered_tool_decision(id, scenario != 0);
        });
    }

    void mcp_verify_mutation(int scenario)
    {
        mcp_wait([self = shared_from_this(), scenario] {
            const auto result = self->mcp_result();
            if (scenario == 1 || scenario == 2) {
                const auto content = result["structuredContent"];
                self->check(result["isError"] == false && content["applied"] == true && content["changes"].size() == 2,
                            "mcp_approved_settings_batch_succeeded");
                self->check(content["projectUndo"] == false, "mcp_settings_explain_project_undo");
                if (scenario == 1) {
                    self->check(content["processPresetDirty"] == true, "mcp_settings_mark_preset_dirty");
                    self->wait_until([self] { return !self->m_plater->get_partplate_list().get_curr_plate()->is_slice_result_valid(); },
                        "mcp_settings_invalidate_real_slice", [self] {
                            self->check(primary_print_action(installed_shell()->status_row()->action_state()).primary.action == PrintAction::Slice,
                                        "settings_change_returns_header_to_slice");
                            self->mcp_mutation(2);
                        });
                    return;
                }
            } else {
                self->check(result["isError"] == true && result["structuredContent"]["error"]["code"] ==
                            (scenario == 0 ? "approval_rejected" : "stale_workspace"), "mcp_settings_refusal_is_structured");
            }
            const auto values = installed_shell()->workspace()->read_settings({"layer_height", "sparse_infill_density"});
            for (const auto& item : values.items)
                self->check(item.value == self->m_settings_original[item.key], "mcp_settings_restored_or_unchanged_" + item.key);
            self->mcp_mutation(scenario + 1);
        });
    }

    void mcp_teardown()
    {
        check(m_plater->model().objects.size() == m_objects_before_tool, "mcp_disconnect_changes_nothing");
        // Hold one approved action across a page reset with no handshake.
        auto& view = installed_shell()->agent_pane()->web_view();
        view.host().reset_page();
        mcp_request(JusPrinTest::request("tools/call", {{"name", "settings_get"}, {"arguments", {{"keys", {"wall_loops"}}}}}));
        mcp_wait([self = shared_from_this()] {
            self->check(self->mcp_result()["isError"] == false, "mcp_read_during_missing_handshake");
            self->m_mcp_client.reset();
            self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "mcp_teardown_project");
            self->finish();
        });
    }

    void agent_send_scripted_message()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        check(!web_view.bridge_error_shown(), "agent_no_bridge_error_after_handshake");
        check(web_view.host().availability() == Agent::AgentAvailability::Ready, "agent_service_ready");

        // Drive the send through the page itself so the user path (page ->
        // script message -> host) is what gets exercised.
        WebView::RunScript(web_view.webview(),
                           "window.__jusprinTest && window.__jusprinTest.send('what is on the plate?')");
        wait_until(
            [&web_view] {
                // Wait for the assistant's own message to finish. Counting
                // entries is not enough: the thread can also hold host notes,
                // which are complete the instant they are posted.
                for (const auto& message : web_view.host().conversation())
                    if (message.role == Agent::MessageRole::Assistant && message.state == Agent::MessageState::Complete)
                        return true;
                return false;
            },
            "agent_reply_streams_to_completion", [self = shared_from_this()] { self->agent_verify_reply_and_context(); });
    }

    void agent_verify_reply_and_context()
    {
        AgentWebView&     web_view = installed_shell()->agent_pane()->web_view();
        Agent::AgentHost& host     = web_view.host();

        // The thread holds turns and host notes. These checks are about the
        // exchange, so they read the turns; a separate check below covers the
        // note the earlier spool swap posted.
        std::vector<Agent::ConversationMessage> turns, notes;
        for (const auto& message : host.conversation())
            (message.role == Agent::MessageRole::Note ? notes : turns).push_back(message);

        check(turns.size() == 2, "agent_conversation_has_exchange");
        check(!turns.empty() && turns.front().role == Agent::MessageRole::User, "agent_user_message_recorded");
        check(!turns.empty() && turns.back().role == Agent::MessageRole::Assistant, "agent_reply_recorded");
        if (turns.empty()) return;
        // The two swaps earlier in this run posted one note each, in order,
        // each naming the spool it swapped to.
        check(notes.size() == 2, "each_spool_swap_posts_one_note");
        if (notes.size() == 2) {
            check(notes[0].text.find("Harness Second Spool") != std::string::npos, "first_swap_note_names_its_spool");
            check(notes[1].text.find("Harness Other Preset") != std::string::npos, "second_swap_note_names_its_spool");
        }
        // A note is a statement, not a turn: nothing replies to it, and it
        // never carries the streaming or failure states a turn can.
        for (const auto& note : notes) {
            check(note.state == Agent::MessageState::Complete, "swap_note_is_complete");
            check(note.in_reply_to.empty(), "swap_note_starts_no_exchange");
        }
        // The reply must describe the authoritative fixture, not canned text:
        // the two-plate fixture and its active plate contents appear in it.
        check(turns.back().text.find("2 plates") != std::string::npos, "agent_reply_describes_fixture_plates");
        check(turns.back().text.find("is active with") != std::string::npos, "agent_reply_describes_active_plate");

        // A native selection change must push fresh context over the bridge.
        const std::uint64_t sent_before = host.messages_sent();
        check(m_plater->select_object(1), "agent_native_selection_change");
        wait_until([&host, sent_before] { return host.messages_sent() > sent_before; },
                   "agent_context_pushed_on_native_selection",
                   [self = shared_from_this()] {
                       self->verify_note_rendered([self] { self->agent_verify_reload(); });
                   });
    }

    void agent_verify_reload()
    {
        AgentWebView& web_view          = installed_shell()->agent_pane()->web_view();
        const std::size_t conversation_size = web_view.host().conversation().size();

        web_view.reload();
        wait_until([&web_view] { return web_view.host().handshake_complete(); }, "agent_reload_handshake",
                   [self = shared_from_this(), conversation_size] {
                       AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
                       self->check(web_view.host().conversation().size() == conversation_size,
                                   "agent_reload_preserves_native_conversation");
                       self->check(!web_view.bridge_error_shown(), "agent_reload_clears_bridge_error");
                       self->agent_tool_propose();
                   });
    }

    // Phase 3: the mock Agent proposes duplicating the selected object; the
    // page's Reject and Approve paths drive the native coordinator; the
    // approved run executes through Orca's own duplicate command; and undo
    // and redo go through Orca's history.
    // Every printed copy in the real model: tool flows add instances.
    std::size_t copy_count() const
    {
        std::size_t count = 0;
        for (const ModelObject* object : m_plater->model().objects)
            count += object->instances.size();
        return count;
    }

    void agent_tool_propose()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        check(m_plater->select_object(0), "tool_target_selected");
        m_objects_before_tool = copy_count();
        const std::size_t activities_before = web_view.host().tools().activities().size();

        WebView::RunScript(web_view.webview(),
                           "window.__jusprinTest && window.__jusprinTest.send('please duplicate the selected object')");
        wait_until(
            [&web_view, activities_before] {
                const auto& activities = web_view.host().tools().activities();
                return activities.size() > activities_before &&
                       activities.back().state == Agent::ToolState::Pending;
            },
            "tool_proposal_pending", [self = shared_from_this()] { self->agent_tool_reject(); });
    }

    void agent_tool_reject()
    {
        AgentWebView&     web_view  = installed_shell()->agent_pane()->web_view();
        const std::string action_id = web_view.host().tools().activities().back().action_id;
        check(web_view.host().tools().activities().back().requires_approval, "tool_mutation_requires_approval");

        WebView::RunScript(web_view.webview(),
                           wxString::FromUTF8("window.__jusprinTest && window.__jusprinTest.decide('" + action_id +
                                              "', 'reject')"));
        wait_until(
            [&web_view, action_id] {
                const Agent::ToolActivity* activity = web_view.host().tools().find(action_id);
                return activity != nullptr && activity->state == Agent::ToolState::Rejected;
            },
            "tool_rejected_via_page", [self = shared_from_this()] {
                self->check(self->copy_count() == self->m_objects_before_tool, "tool_rejection_changes_nothing");
                self->agent_tool_approve();
            });
    }

    void agent_tool_approve()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        const std::size_t activities_before = web_view.host().tools().activities().size();
        WebView::RunScript(web_view.webview(),
                           "window.__jusprinTest && window.__jusprinTest.send('duplicate it after all')");
        wait_until(
            [&web_view, activities_before] {
                const auto& activities = web_view.host().tools().activities();
                return activities.size() > activities_before &&
                       activities.back().state == Agent::ToolState::Pending;
            },
            "tool_second_proposal_pending", [self = shared_from_this()] {
                AgentWebView&     web_view  = installed_shell()->agent_pane()->web_view();
                const std::string action_id = web_view.host().tools().activities().back().action_id;
                WebView::RunScript(web_view.webview(),
                                   wxString::FromUTF8("window.__jusprinTest && window.__jusprinTest.decide('" + action_id +
                                                      "', 'approve')"));
                self->wait_until(
                    [&web_view, action_id] {
                        const Agent::ToolActivity* activity = web_view.host().tools().find(action_id);
                        return activity != nullptr && activity->state == Agent::ToolState::Succeeded;
                    },
                    "tool_approved_and_succeeded", [self] { self->agent_tool_verify_undo(); });
            });
    }

    void agent_tool_verify_undo()
    {
        // The approved duplicate is authoritative Orca state and one Orca
        // history step.
        check(copy_count() == m_objects_before_tool + 1, "tool_duplicate_visible_in_model");
        check(m_plater->canvas3D()->get_volumes_count() >= 3, "tool_duplicate_visible_on_canvas");
        check(m_plater->can_undo_project(), "tool_change_is_undoable");
        check(m_plater->undo_project(), "tool_undo_through_orca");
        check(copy_count() == m_objects_before_tool, "tool_undo_removes_duplicate");
        check(m_plater->redo_project(), "tool_redo_through_orca");
        check(copy_count() == m_objects_before_tool + 1, "tool_redo_restores_duplicate");
        agent_conversations();
    }

    // Phase 4: conversations, project-owned persistence, save/reopen,
    // import identity, and the clean-sharing copy — all against the real
    // application and real 3MF archives.
    Agent::ProjectPersistence& persistence() { return *installed_shell()->persistence(); }

    void agent_conversations()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        check(persistence().document().has_identity(), "project_document_has_identity");
        check(persistence().document().conversations().size() == 1, "starts_with_one_conversation");
        const std::size_t conversations_before = persistence().document().conversations().size();

        WebView::RunScript(web_view.webview(), "window.__jusprinTest && window.__jusprinTest.createConversation()");
        wait_until(
            [this, conversations_before] {
                return persistence().document().conversations().size() == conversations_before + 1;
            },
            "conversation_created_via_page", [self = shared_from_this()] {
                AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
                const std::size_t messages_before = web_view.host().conversation().size();
                self->check(messages_before == 0, "new_conversation_starts_empty");
                WebView::RunScript(web_view.webview(),
                                   "window.__jusprinTest && window.__jusprinTest.send('what changed so far?')");
                self->wait_until(
                    [&web_view] {
                        // Same reason as the first exchange: wait for the
                        // assistant's message, not for a count that a host
                        // note could also satisfy.
                        for (const auto& message : web_view.host().conversation())
                            if (message.role == Agent::MessageRole::Assistant &&
                                message.state == Agent::MessageState::Complete)
                                return true;
                        return false;
                    },
                    "second_conversation_reply_completes", [self] { self->agent_manage_chats(); });
            });
    }

    void agent_manage_chats()
    {
        const auto id = persistence().document().active_conversation_id();
        wait_until([this, id] { return !persistence().document().needs_conversation_title(id); },
                   "chat_title_generated_after_exchange", [self = shared_from_this(), id] {
            auto& view = installed_shell()->agent_pane()->web_view();
            WebView::RunScript(view.webview(), wxString::FromUTF8(
                "window.__jusprinTest.renameConversation('" + id + "', 'Backpack frame test')"));
            self->wait_until([self] { return self->persistence().document().conversations().front().title == "Backpack frame test"; },
                             "chat_renamed_via_page", [self, id] {
                const auto revision = installed_shell()->workspace()->snapshot().revision;
                auto& view = installed_shell()->agent_pane()->web_view();
                WebView::RunScript(view.webview(), "window.__jusprinTest.createConversation()");
                self->wait_until([self] { return self->persistence().document().conversations().size() == 3; },
                                 "disposable_chat_created", [self, id, revision] {
                    const auto disposable = self->persistence().document().active_conversation_id();
                    auto& view = installed_shell()->agent_pane()->web_view();
                    WebView::RunScript(view.webview(), wxString::FromUTF8(
                        "window.__jusprinTest.deleteConversation('" + disposable + "')"));
                    self->wait_until([self] { return self->persistence().document().conversations().size() == 2; },
                                     "chat_deleted_via_page", [self, id, revision] {
                        self->check(self->persistence().document().active_conversation_id() == id, "deletion_returns_to_recent_chat");
                        self->check(installed_shell()->workspace()->snapshot().revision == revision, "chat_management_preserves_model");
                        self->agent_save_reopen();
                    });
                });
            });
        });
    }

    void agent_save_reopen()
    {
        m_saved_project_id  = persistence().document().project_id();
        m_saved_project_file = (fs::temp_directory_path() / fs::unique_path("jusprin-phase4-%%%%.3mf")).string();
        // The same strategy Plater::save_project uses, silenced; the
        // auxiliary dir (with state.json) is included.
        const auto save_started = std::chrono::steady_clock::now();
        check(m_plater->export_3mf(boost::filesystem::path(m_saved_project_file),
                                   SaveStrategy::SplitModel | SaveStrategy::ShareMesh | SaveStrategy::Silence) >= 0,
              "project_saved_with_state");
        m_save_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - save_started).count();
        std::ifstream saved(m_saved_project_file, std::ios::binary);
        const std::string saved_bytes((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
        m_saved_project_bytes = saved_bytes.size();
        check(saved_bytes.find("JusPrin/state.json") != std::string::npos, "saved_archive_contains_state_json");
        // JusPrin keeps no project copies of its own inside the project.
        check(saved_bytes.find(".snapshot") == std::string::npos, "saved_archive_has_no_project_copies");

        check(m_plater->new_project(true, true) != wxID_CANCEL, "phase4_new_project");
        wait_until(
            [this] {
                return persistence().document().has_identity() && persistence().document().project_id() != m_saved_project_id;
            },
            "new_project_starts_new_identity", [self = shared_from_this()] {
                self->verify_project_open_answers_dialogs();
                self->verify_step_import();
                self->verify_support_settings_patch();
                self->verify_regions();
                self->verify_reshape();
                self->verify_slice_checks([self] {
                    self->wait_until(
                        [self] { return self->persistence().document().project_id() == self->m_saved_project_id; },
                        "saved_state_adopted_on_reopen", [self] {
                            self->check(self->persistence().document().conversations().size() == 2,
                                        "saved_conversations_survive_reopen");
                            self->check(self->persistence().document().conversations().front().title == "Backpack frame test",
                                        "renamed_chat_survives_project_reopen");
                            const auto messages = self->persistence().document().messages(
                                self->persistence().document().active_conversation_id());
                            self->check(!messages.empty(), "saved_messages_survive_reopen");
                            self->agent_phase6_history();
                        });
                });
            });
    }

    // Counts every dialog that becomes visible, other than the load progress
    // window, while it is installed.
    struct DialogCounter : wxEventFilter
    {
        int  shown = 0;
        std::vector<std::string> titles;
        DialogCounter() { wxEvtHandler::AddFilter(this); }
        ~DialogCounter() override { wxEvtHandler::RemoveFilter(this); }
        int FilterEvent(wxEvent& event) override
        {
            if (event.GetEventType() == wxEVT_SHOW) {
                auto* dialog = dynamic_cast<wxDialog*>(event.GetEventObject());
                // load_files' progress window asks nothing; it is titled, not typed.
                if (dialog != nullptr && static_cast<wxShowEvent&>(event).IsShown() &&
                    !dialog->GetTitle().StartsWith(wxGetTranslation("Loading"))) {
                    ++shown;
                    titles.push_back(dialog->GetTitle().ToUTF8().data());
                }
            }
            return Event_Skip;
        }
    };

    // project_open through the real adapter: a model saved in metres asks
    // "Object too small" and a dirty project asks to be saved; both are
    // answered from the request and neither may reach the screen.
    void verify_project_open_answers_dialogs()
    {
        const fs::path tiny = fs::temp_directory_path() / fs::unique_path("jusprin-metres-%%%%.stl");
        {
            // A 20 mm cube written in metres.
            std::ofstream out(tiny.string());
            out << "solid tiny" << std::endl;
            const double s = 0.02;
            const double v[8][3] = {{0,0,0},{s,0,0},{s,s,0},{0,s,0},{0,0,s},{s,0,s},{s,s,s},{0,s,s}};
            const int f[12][3] = {{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}};
            for (const auto& t : f) {
                out << "facet normal 0 0 0" << std::endl << "outer loop" << std::endl;
                for (int i : t) out << "vertex " << v[i][0] << " " << v[i][1] << " " << v[i][2] << std::endl;
                out << "endloop" << std::endl << "endfacet" << std::endl;
            }
            out << "endsolid tiny" << std::endl;
        }
        auto* workspace = installed_shell()->workspace();
        Workspace::ProjectOpenRequest request;
        request.path            = tiny.string();
        request.units           = Workspace::UnitChoice::ConvertIfTiny;
        request.discard_unsaved = true;
        std::vector<Workspace::LoadDecision> decisions;
        {
            DialogCounter counter;
            check(workspace->open_project(request, decisions).succeeded(), "project_open_model_file");
            for (const auto& title : counter.titles) std::cout << "project_open dialog shown: " << title << std::endl;
            check(counter.shown == 0, "project_open_model_shows_no_dialog");
        }
        check(std::any_of(decisions.begin(), decisions.end(),
                          [](const Workspace::LoadDecision& d) { return d.answer == "yes"; }),
              "project_open_answers_object_too_small");
        const BoundingBoxf3 box = m_plater->model().objects.empty() ? BoundingBoxf3() : m_plater->model().objects.front()->bounding_box_exact();
        check(m_plater->model().objects.size() == 1 && std::abs(box.size().x() - 20.) < 0.01,
              "project_open_converted_metres");

        // Make the project dirty, then reopen the saved project over it.
        check(m_plater->duplicate_object(0) >= 0, "project_open_dirty_before_reopen");
        request      = {};
        request.path = m_saved_project_file;
        decisions.clear();
        check(!workspace->open_project(request, decisions).succeeded(), "project_open_refuses_unsaved_work");
        request.discard_unsaved = true;
        {
            DialogCounter counter;
            const auto opened = workspace->open_project(request, decisions);
            if (!opened.succeeded()) std::cout << "project_open failed: " << opened.message << std::endl;
            check(opened.succeeded(), "project_open_project_file");
            for (const auto& title : counter.titles) std::cout << "project_open dialog shown: " << title << std::endl;
            check(counter.shown == 0, "project_open_project_shows_no_dialog");
        }
        // Orca asks to save only when the person has not told it to stop
        // asking; whatever it asked was declined.
        for (const auto& decision : decisions) std::cout << "project_open asked: " << decision.question << " -> " << decision.answer << std::endl;
        check(std::all_of(decisions.begin(), decisions.end(), [](const Workspace::LoadDecision& d) { return d.answer == "no"; }),
              "project_open_declines_saving");
        fs::remove(tiny);
    }

    // M4's settings through the real adapter: a tree-support patch on the
    // build plate with a brim reaches the support page's fields and asks
    // nothing; a support style that does not fit is refused; an object
    // override lands in the object's ModelConfig as one undo step.
    void verify_support_settings_patch()
    {
        auto* workspace = installed_shell()->workspace();
        auto* tab       = wxGetApp().get_tab(Preset::TYPE_PRINT);
        auto& prints    = wxGetApp().preset_bundle->prints;
        const DynamicPrintConfig original = prints.get_edited_preset().config;
        tab->activate_option("enable_support", "Support");
        const Workspace::SettingsPatch supports{{{"enable_support", "1"}, {"support_type", "tree(auto)"},
                                                 {"support_style", "organic"}, {"support_on_build_plate_only", "1"},
                                                 {"brim_type", "outer_only"}, {"brim_width", "6"}}};
        {
            DialogCounter counter;
            const auto preview = workspace->preview_settings(supports);
            for (const auto& issue : preview.issues) std::cout << "support patch issue: " << issue.key << " " << issue.message << std::endl;
            check(preview.valid, "settings_support_patch_valid");
            Workspace::SettingsPreview applied;
            check(workspace->apply_settings(supports, Workspace::settings_confirmation(preview), applied).succeeded(),
                  "settings_support_patch_applies");
            for (const auto& title : counter.titles) std::cout << "settings dialog shown: " << title << std::endl;
            check(counter.shown == 0, "settings_support_patch_shows_no_dialog");
        }
        const auto& config = prints.get_edited_preset().config;
        for (const auto& [key, value] : supports.changes)
            check(config.option(key)->serialize() == value, "settings_support_value_" + key);
        for (const char* key : {"enable_support", "support_on_build_plate_only"}) {
            Field* shown = tab->get_field(key);
            check(shown != nullptr && boost::any_cast<bool>(shown->get_value()), std::string("settings_support_field_shows_") + key);
        }
        const auto mismatch = workspace->preview_settings({{{"support_style", "grid"}}});
        check(!mismatch.valid && mismatch.issues.front().key == "support_style" &&
                  std::count(mismatch.issues.front().allowed.begin(), mismatch.issues.front().allowed.end(), "organic") == 1,
              "settings_support_style_mismatch_refused");
        check(workspace->preview_settings({{{"support_style", "tree_strong"}}}).valid, "settings_support_style_fitting_accepted");
        check(workspace->preview_settings({{{"support_on_build_plate_only", "0"}}}).valid, "settings_boolean_has_no_bounds");
        Workspace::SettingsQuery changed;
        changed.limit        = 25;
        changed.changed_only = true;
        const auto unsaved   = workspace->search_settings(changed);
        check(std::any_of(unsaved.items.begin(), unsaved.items.end(), [](const auto& item) { return item.key == "brim_width"; }) &&
                  std::none_of(unsaved.items.begin(), unsaved.items.end(), [](const auto& item) { return item.key == "layer_height"; }),
              "settings_search_changed_only");

        // One object's override.
        const auto snapshot = workspace->snapshot();
        check(!snapshot.plates.empty() && !snapshot.plates[0].objects.empty(), "settings_object_present");
        if (snapshot.plates.empty() || snapshot.plates[0].objects.empty()) return;
        Workspace::SettingsTarget target{snapshot.plates[0].objects[0].id};
        const ModelObject* object = m_plater->model().objects.front();
        const Workspace::SettingsPatch walls{{{"wall_loops", "5"}}, target};
        {
            DialogCounter counter;
            const auto preview = workspace->preview_settings(walls);
            check(preview.valid, "settings_object_patch_valid");
            Workspace::SettingsPreview applied;
            check(workspace->apply_settings(walls, Workspace::settings_confirmation(preview), applied).succeeded(),
                  "settings_object_patch_applies");
            check(counter.shown == 0, "settings_object_patch_shows_no_dialog");
        }
        check(object->config.has("wall_loops") && object->config.opt_int("wall_loops") == 5, "settings_object_override_in_model");
        check(config.opt_int("wall_loops") == original.opt_int("wall_loops"), "settings_object_leaves_process_alone");
        const auto read = workspace->read_settings({"wall_loops"}, target);
        check(read.items.size() == 1 && read.items[0].value == "5" && read.items[0].overridden == true, "settings_object_read_back");
        check(workspace->snapshot().can_undo, "settings_object_is_undoable");
        check(workspace->preview_settings({{{"skirt_loops", "2"}}, target}).issues.front().code == "unsupported_scope",
              "settings_object_refuses_print_scope");
        m_plater->undo();
        check(!object->config.has("wall_loops"), "settings_object_undo_removes_override");
        tab->load_config(original);
    }

    // A STEP file through the real adapter: Orca asks how finely to mesh it,
    // and the request takes Orca's own defaults without showing the dialog.
    void verify_step_import()
    {
        auto* workspace = installed_shell()->workspace();
        const std::size_t before = m_plater->model().objects.size();
        Workspace::ImportRequest import;
        import.path = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/jusprin/box_20x15x10.step";
        std::vector<Workspace::LoadDecision> decisions;
        std::vector<Workspace::ObjectId>     added;
        {
            DialogCounter counter;
            const auto imported = workspace->import_objects(import, decisions, added);
            if (!imported.succeeded()) std::cout << "step import failed: " << imported.message << std::endl;
            check(imported.succeeded() && added.size() == 1, "step_import_succeeds");
            for (const auto& title : counter.titles) std::cout << "step import dialog shown: " << title << std::endl;
            check(counter.shown == 0, "step_import_shows_no_dialog");
        }
        for (const auto& decision : decisions) std::cout << "step import asked: " << decision.question << " -> " << decision.answer << std::endl;
        check(m_plater->model().objects.size() == before + 1, "step_import_adds_one_object");
        if (m_plater->model().objects.size() == before + 1) {
            const Vec3d size = m_plater->model().objects.back()->bounding_box_exact().size();
            check(std::abs(size.x() - 20.) < 0.01 && std::abs(size.y() - 15.) < 0.01 && std::abs(size.z() - 10.) < 0.01,
                  "step_import_keeps_the_size");
            check(workspace->undo().succeeded() && m_plater->model().objects.size() == before, "step_import_undone");
        }
    }

    // Regions through the real adapter, on a T bar with a 10 mm hole through
    // it: a precision hole becomes a support blocker filling the hole, a face
    // becomes seam paint; Undo strands the record, Redo restores it, a turn
    // keeps it bound, and removal takes the artifacts away.
    void verify_regions()
    {
        auto* workspace = installed_shell()->workspace();
        std::vector<Workspace::LoadDecision> decisions;
        std::vector<Workspace::ObjectId>     added;
        Workspace::ImportRequest             import;
        import.path = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/jusprin/tee_with_hole.stl";
        check(workspace->import_objects(import, decisions, added).succeeded() && added.size() == 1, "regions_fixture_imported");
        if (added.size() != 1) return;
        const Workspace::ObjectId tee = added.front();
        ModelObject* object = nullptr;
        for (ModelObject* candidate : m_plater->model().objects)
            if (candidate->id().id == tee.value()) object = candidate;

        Workspace::AnalysisRequest wanted;
        wanted.features = true;
        Workspace::ObjectAnalysis analysis;
        check(workspace->analyze_object(tee, wanted, analysis).succeeded() && analysis.features, "regions_features_read");
        if (!analysis.features) return;
        const auto hole = std::find_if(analysis.features->holes.begin(), analysis.features->holes.end(),
                                       [](const Workspace::HoleFeature& h) { return std::abs(h.diameter - 10) < 0.2; });
        const auto top = std::find_if(analysis.features->faces.begin(), analysis.features->faces.end(),
                                      [](const Workspace::FaceFeature& f) { return f.normal[2] > 0.99 && f.area > 900; });
        check(hole != analysis.features->holes.end(), "regions_hole_found");
        check(top != analysis.features->faces.end(), "regions_top_face_found");
        if (hole == analysis.features->holes.end() || top == analysis.features->faces.end()) return;

        std::vector<Workspace::RegionRequest> requests(2);
        requests[0].object   = tee;
        requests[0].kind     = "precision_hole";
        requests[0].geometry = Workspace::RegionGeometry{"hole", hole->handle};
        requests[1].object   = tee;
        requests[1].kind     = "seam_preferred";
        requests[1].geometry = Workspace::RegionGeometry{"face", top->handle};
        std::vector<Workspace::RegionRecord> planned, applied;
        const auto plan = workspace->plan_regions(requests, {}, planned);
        if (!plan.succeeded()) std::cout << "regions plan: " << plan.message << std::endl;
        check(plan.succeeded() && planned.size() == 2, "regions_planned");
        if (planned.size() != 2) return;
        std::cout << "regions: " << planned[0].label << " | " << planned[1].label << std::endl;
        check(std::abs(planned[0].geometry.length - 20) < 0.05, "regions_hole_depth_is_through");
        check(planned[1].artifacts.size() == 1 && !planned[1].artifacts[0].facets.empty(), "regions_face_facets_resolved");
        const std::size_t volumes_before = object->volumes.size();
        {
            DialogCounter counter;
            check(workspace->apply_regions(planned, {}, applied).succeeded(), "regions_applied");
            check(counter.shown == 0, "regions_show_no_dialog");
        }
        check(object->volumes.size() == volumes_before + 1 && object->volumes.back()->is_support_blocker() &&
                  object->volumes.back()->name == "JusPrin r1 support blocker",
              "regions_blocker_added");
        // The blocker fills the hole and a millimetre around it: 12 mm across,
        // 22 mm along Y, centred on the hole's axis in the object's frame.
        const BoundingBoxf3 blocker = object->volumes.back()->mesh().transformed_bounding_box(object->volumes.back()->get_matrix());
        const BoundingBoxf3 part    = object->volumes.front()->mesh().transformed_bounding_box(object->volumes.front()->get_matrix());
        std::cout << "blocker size " << blocker.size().transpose() << " centre " << blocker.center().transpose()
                  << " part centre " << part.center().transpose() << std::endl;
        check(std::abs(blocker.size().y() - 22) < 0.1 && std::abs(blocker.size().x() - 12) < 0.2 &&
                  std::abs(blocker.size().z() - 12) < 0.2,
              "regions_blocker_fills_the_hole");
        check(std::abs(blocker.center().x() - part.center().x()) < 0.1 && std::abs(blocker.center().y() - part.center().y()) < 0.1 &&
                  std::abs(blocker.center().z() - (part.min.z() + 28)) < 0.1,
              "regions_blocker_on_the_hole_axis");
        check(object->volumes.front()->seam_facets.has_facets(*object->volumes.front(), EnforcerBlockerType::ENFORCER),
              "regions_seam_painted");

        auto status = [&] { return workspace->region_status(applied); };
        auto all = [](const std::vector<Workspace::RegionStatus>& statuses, bool Workspace::RegionStatus::*field, bool value) {
            return std::all_of(statuses.begin(), statuses.end(), [&](const auto& s) { return s.object && s.*field == value; });
        };
        check(all(status(), &Workspace::RegionStatus::binding_lost, false) && all(status(), &Workspace::RegionStatus::artifacts_missing, false),
              "regions_status_bound_and_present");
        m_plater->undo();
        check(all(status(), &Workspace::RegionStatus::artifacts_missing, true) && all(status(), &Workspace::RegionStatus::binding_lost, false),
              "regions_undo_strands_the_record");
        m_plater->redo();
        check(all(status(), &Workspace::RegionStatus::artifacts_missing, false), "regions_redo_restores_artifacts");

        // Turning the object is not a mesh change.
        Workspace::PlacementRequest turn;
        turn.rotate = Workspace::Vec3{0, 0, 90};
        Workspace::PlacementResult turned;
        check(workspace->place_object(tee, turn, "regions-turn", turned).succeeded(), "regions_object_turned");
        check(all(status(), &Workspace::RegionStatus::binding_lost, false) && all(status(), &Workspace::RegionStatus::artifacts_missing, false),
              "regions_survive_a_turn");

        check(workspace->remove_regions(applied).succeeded(), "regions_removed");
        check(object->volumes.size() == volumes_before &&
                  !object->volumes.front()->seam_facets.has_facets(*object->volumes.front(), EnforcerBlockerType::ENFORCER),
              "regions_removal_takes_the_artifacts");
        check(all(status(), &Workspace::RegionStatus::artifacts_missing, true), "regions_status_after_removal");
        check(workspace->delete_items({Workspace::DeleteItem{Workspace::DeleteItem::Kind::Object, tee}}).succeeded(), "regions_fixture_removed");
    }

    // Reshaping through the real adapter: a preview that changes nothing, a
    // plane cut, a merge of the pieces, a split back into shells, and a
    // repair of a mesh with a hole in it, none of which may ask anything.
    void verify_reshape()
    {
        auto* workspace = installed_shell()->workspace();
        auto import_file = [&](const std::string& path) -> std::optional<Workspace::ObjectId> {
            std::vector<Workspace::LoadDecision> decisions;
            std::vector<Workspace::ObjectId>     added;
            Workspace::ImportRequest             import;
            import.path = path;
            if (!workspace->import_objects(import, decisions, added).succeeded() || added.size() != 1)
                return std::nullopt;
            return added.front();
        };
        const auto tee = import_file(std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/jusprin/tee_with_hole.stl");
        check(tee.has_value(), "reshape_fixture_imported");
        if (!tee) return;
        const auto count = [this] { return m_plater->model().objects.size(); };
        const std::size_t objects_before = count();

        // The bar is 36 mm tall and centred; cut it through the stem.
        const BoundingBoxf3 box = m_plater->model().objects.back()->instance_bounding_box(0);
        Workspace::DivideRequest cut;
        cut.point  = {box.center().x(), box.center().y(), box.min.z() + 10};
        cut.normal = {0, 0, 1};
        Workspace::DivideResult preview;
        const auto revision = workspace->snapshot().revision;
        const auto steps    = workspace->history().steps.size();
        const bool dirty    = m_plater->is_project_dirty();
        {
            DialogCounter counter;
            check(workspace->preview_divide(*tee, cut, preview).succeeded() && preview.pieces.size() == 2, "reshape_preview_two_pieces");
            check(counter.shown == 0, "reshape_preview_shows_no_dialog");
        }
        for (const auto& piece : preview.pieces)
            std::cout << "preview piece " << piece.name << " size " << piece.size[0] << " " << piece.size[1] << " " << piece.size[2]
                      << " overhang " << piece.overhang_area << std::endl;
        std::cout << "overhang before " << preview.overhang_area_before << std::endl;
        check(count() == objects_before && workspace->snapshot().revision == revision &&
                  workspace->history().steps.size() == steps && m_plater->is_project_dirty() == dirty,
              "reshape_preview_changes_nothing");
        check(preview.overhang_area_before > 500 && preview.pieces.size() == 2 &&
                  std::abs(preview.pieces[0].size[2] + preview.pieces[1].size[2] - 36) < 0.2,
              "reshape_preview_heights_add_up");

        Workspace::DivideResult divided;
        {
            DialogCounter counter;
            check(workspace->divide_object(*tee, cut, divided).succeeded(), "reshape_cut");
            check(counter.shown == 0, "reshape_cut_shows_no_dialog");
        }
        check(count() == objects_before + 1 && divided.pieces.size() == 2 && divided.pieces[0].object && divided.pieces[1].object,
              "reshape_cut_makes_two_objects");
        if (divided.pieces.size() != 2 || !divided.pieces[0].object || !divided.pieces[1].object) return;

        Workspace::ObjectId merged;
        {
            DialogCounter counter;
            check(workspace->merge_objects({*divided.pieces[0].object, *divided.pieces[1].object}, merged).succeeded(), "reshape_merge");
            check(counter.shown == 0, "reshape_merge_shows_no_dialog");
        }
        check(count() == objects_before && m_plater->model().objects.back()->volumes.size() == 2, "reshape_merge_makes_one_object_of_two_parts");

        Workspace::DivideRequest shells;
        shells.mode = Workspace::DivideRequest::Mode::Shells;
        Workspace::DivideResult split;
        check(workspace->divide_object(merged, shells, split).succeeded() && split.pieces.size() == 2 && count() == objects_before + 1,
              "reshape_split_to_objects");
        for (const auto& piece : split.pieces)
            if (piece.object)
                workspace->delete_items({Workspace::DeleteItem{Workspace::DeleteItem::Kind::Object, *piece.object}});

        // A 20 mm cube with one facet missing.
        const fs::path open = fs::temp_directory_path() / fs::unique_path("jusprin-open-%%%%.stl");
        {
            std::ofstream out(open.string());
            out << "solid open" << std::endl;
            const double s = 20;
            const double v[8][3] = {{0,0,0},{s,0,0},{s,s,0},{0,s,0},{0,0,s},{s,0,s},{s,s,s},{0,s,s}};
            const int f[11][3] = {{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4}};
            for (const auto& tri : f) {
                out << "facet normal 0 0 0" << std::endl << "outer loop" << std::endl;
                for (int i : tri) out << "vertex " << v[i][0] << " " << v[i][1] << " " << v[i][2] << std::endl;
                out << "endloop" << std::endl << "endfacet" << std::endl;
            }
            out << "endsolid open" << std::endl;
        }
        const auto broken = import_file(open.string());
        check(broken.has_value(), "reshape_open_mesh_imported");
        if (broken) {
            Workspace::RepairResult repaired;
            {
                DialogCounter counter;
                check(workspace->repair_object(*broken, repaired).succeeded(), "reshape_repair");
                check(counter.shown == 0, "reshape_repair_shows_no_dialog");
            }
            std::cout << "repair open edges " << repaired.open_edges_before << " -> " << repaired.open_edges_after << ", facets "
                      << repaired.facets_before << " -> " << repaired.facets_after << std::endl;
            check(repaired.changed && repaired.open_edges_before > 0 && repaired.open_edges_after == 0, "reshape_repair_closes_the_mesh");
            Workspace::RepairResult again;
            check(workspace->repair_object(*broken, again).succeeded() && !again.changed, "reshape_repair_of_a_closed_mesh_changes_nothing");
            workspace->delete_items({Workspace::DeleteItem{Workspace::DeleteItem::Kind::Object, *broken}});
        }
        fs::remove(open);
    }

    // The slice checks on a real slice: the T bar with supports on prints
    // support inside its hole, which the report finds without any region;
    // once the hole is a precision hole and the back face is hidden, a new
    // slice keeps support out of the hole and puts seams on that face.
    void verify_slice_checks(std::function<void()> then)
    {
        auto* workspace = installed_shell()->workspace();
        auto& prints    = wxGetApp().preset_bundle->prints;
        m_slice_check_settings = prints.get_edited_preset().config;
        std::vector<Workspace::LoadDecision> decisions;
        std::vector<Workspace::ObjectId>     added;
        Workspace::ImportRequest             import;
        import.path = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/jusprin/tee_with_hole.stl";
        const bool imported = workspace->import_objects(import, decisions, added).succeeded() && added.size() == 1;
        check(imported, "slice_checks_fixture_imported");
        const Workspace::SettingsPatch supports{{{"enable_support", "1"}, {"support_type", "normal(auto)"},
                                                 {"support_on_build_plate_only", "0"}}};
        Workspace::SettingsPreview applied;
        check(workspace->apply_settings(supports, Workspace::settings_confirmation(workspace->preview_settings(supports)), applied).succeeded(),
              "slice_checks_supports_on");
        const auto plate = workspace->snapshot().active_plate;
        if (!imported || !plate) {
            then();
            return;
        }
        m_slice_check_object = added.front();
        slice_then("slice_checks_first_slice", *plate, [self = shared_from_this(), plate, then] {
            auto* workspace = installed_shell()->workspace();
            Workspace::SliceReportRequest request;
            request.supports = request.seams = request.first_layer = request.islands = true;
            const auto report = workspace->slice_report(*plate, request);
            self->check(report.valid && report.supports && report.supports->generated, "slice_checks_supports_generated");
            const auto hole_contact = [self](const Workspace::SliceReport& r) {
                return r.supports && std::any_of(r.supports->contacts.begin(), r.supports->contacts.end(), [&](const auto& c) {
                           return c.object && *c.object == self->m_slice_check_object;
                       });
            };
            if (report.supports)
                for (const auto& contact : report.supports->contacts)
                    std::cout << "support contact " << contact.object_name << " " << contact.target << " " << contact.area
                              << " mm2 over " << contact.layers << " layers" << std::endl;
            self->check(hole_contact(report), "slice_checks_support_enters_the_hole");
            self->check(report.first_layer && std::any_of(report.first_layer->objects.begin(), report.first_layer->objects.end(),
                                                          [&](const auto& o) { return o.object && *o.object == self->m_slice_check_object &&
                                                                                      std::abs(o.contact_area - 320) < 20; }),
                        "slice_checks_first_layer_is_the_stem");
            self->check(report.islands && report.seams && report.seams->count > 0, "slice_checks_islands_and_seams_read");

            // Annotate the hole and the back face.
            Workspace::AnalysisRequest wanted;
            wanted.features = true;
            Workspace::ObjectAnalysis analysis;
            workspace->analyze_object(self->m_slice_check_object, wanted, analysis);
            const auto& features = *analysis.features;
            const auto hole = std::find_if(features.holes.begin(), features.holes.end(),
                                           [](const auto& h) { return std::abs(h.diameter - 10) < 0.2; });
            const auto back = std::find_if(features.faces.begin(), features.faces.end(),
                                           [](const auto& f) { return f.normal[1] > 0.99; });
            if (hole == features.holes.end() || back == features.faces.end()) {
                self->check(false, "slice_checks_features_found");
                self->finish_slice_checks(then);
                return;
            }
            std::vector<Workspace::RegionRequest> requests(2);
            requests[0].object   = self->m_slice_check_object;
            requests[0].kind     = "precision_hole";
            requests[0].geometry = Workspace::RegionGeometry{"hole", hole->handle};
            requests[1].object   = self->m_slice_check_object;
            requests[1].kind     = "hidden";
            requests[1].geometry = Workspace::RegionGeometry{"face", back->handle};
            std::vector<Workspace::RegionRecord> planned;
            self->check(workspace->plan_regions(requests, {}, planned).succeeded() &&
                            workspace->apply_regions(planned, {}, self->m_slice_check_regions).succeeded(),
                        "slice_checks_regions_applied");
            self->slice_then("slice_checks_second_slice", *plate, [self, plate, then, hole_contact] {
                auto* workspace = installed_shell()->workspace();
                Workspace::SliceReportRequest request;
                request.supports = request.seams = true;
                request.regions  = self->m_slice_check_regions;
                const auto report = workspace->slice_report(*plate, request);
                if (report.supports)
                    for (const auto& contact : report.supports->contacts)
                        std::cout << "support contact after " << contact.object_name << " " << contact.target << " " << contact.area << std::endl;
                self->check(report.valid && !hole_contact(report), "slice_checks_precision_hole_keeps_support_out");
                const bool seams_on_back = report.seams && std::any_of(report.seams->regions.begin(), report.seams->regions.end(),
                                                                       [](const auto& r) { return r.region_id == "r2" && r.seams > 0; });
                if (report.seams)
                    for (const auto& placement : report.seams->regions)
                        std::cout << "seams on " << placement.region_id << " " << placement.kind << ": " << placement.seams << " of "
                                  << report.seams->count << std::endl;
                self->check(seams_on_back, "slice_checks_seams_on_the_hidden_face");
                self->verify_outputs(*plate);
                self->verify_cancel(*plate, [self, then] { self->finish_slice_checks(then); });
            });
        });
    }

    // M6 through the real adapter, on the sliced plate: a rendered picture,
    // a packed picture read back, the slice's layers and G-code, every export
    // kind, and a cancelled slice; none may ask anything.
    void verify_outputs(Workspace::PlateId plate)
    {
        auto* workspace = installed_shell()->workspace();
        DialogCounter counter;

        Workspace::RenderRequest render;
        render.plate  = plate;
        render.view   = "front";
        render.width  = 640;
        render.height = 480;
        Workspace::RenderedImage picture;
        const auto rendered = workspace->render_view(render, picture);
        if (!rendered.succeeded()) std::cout << "render: " << rendered.message << std::endl;
        check(rendered.succeeded() && picture.width == 640 && picture.height == 480 && picture.png.size() > 100 &&
                  picture.png.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)) == 0,
              "outputs_render_is_a_png");
        // A plain background would compress to almost nothing; the plate's
        // objects make a picture of some size.
        std::cout << "rendered front view: " << picture.png.size() << " bytes" << std::endl;
        check(picture.png.size() > 2000, "outputs_render_shows_something");

        // A packed picture, larger than the cap, comes back scaled.
        const fs::path pictures = fs::path(workspace->auxiliary_data_dir()) / "Model Pictures";
        fs::create_directories(pictures);
        {
            wxImage large(2000, 1000);
            large.SetRGB(wxRect(0, 0, 2000, 1000), 200, 60, 30);
            large.SaveFile(wxString::FromUTF8((pictures / "cover.png").string()), wxBITMAP_TYPE_PNG);
            std::ofstream((pictures / "notes.txt").string()) << "Print the bracket in PETG.";
        }
        Workspace::AttachmentContent cover, notes, escape;
        check(workspace->read_attachment("Model Pictures/cover.png", cover).succeeded() && cover.kind == "image" &&
                  cover.width == 1280 && cover.height == 640 && cover.truncated,
              "outputs_attachment_picture_scaled");
        check(workspace->read_attachment("Model Pictures/notes.txt", notes).succeeded() && notes.kind == "text" &&
                  notes.data == "Print the bracket in PETG.",
              "outputs_attachment_text");
        check(!workspace->read_attachment("../Metadata/model_settings.config", escape).succeeded() &&
                  !workspace->read_attachment("JusPrin/state.json", escape).succeeded(),
              "outputs_attachment_stays_in_the_folder");
        fs::remove_all(pictures);

        Workspace::SliceInspectRequest inspect;
        inspect.plate = plate;
        inspect.count = 5;
        const auto layers = workspace->inspect_slice(inspect);
        check(layers.valid && layers.layer_count > 100 && layers.layers.size() == 5 && layers.next == 5 &&
                  std::abs(layers.layers[0].z - 0.2) < 0.01 && layers.layers[1].seconds > 0 && !layers.layers[1].roles.empty(),
              "outputs_inspect_layers");
        if (layers.layers.size() > 1)
            std::cout << "layer 1: z " << layers.layers[1].z << " time " << layers.layers[1].seconds << " s, speed "
                      << layers.layers[1].speed_min << "-" << layers.layers[1].speed_max << ", roles " << layers.layers[1].roles.size()
                      << std::endl;
        inspect.gcode = true;
        const auto gcode = workspace->inspect_slice(inspect);
        check(gcode.valid && !gcode.gcode.empty() && gcode.gcode.size() <= 64 * 1024 && gcode.next.has_value(), "outputs_inspect_gcode");

        const fs::path folder = fs::temp_directory_path() / fs::unique_path("jusprin-exports-%%%%");
        fs::create_directories(folder);
        const auto exported = [&](const std::string& kind, const fs::path& path, const char* name) {
            Workspace::ExportRequest request;
            request.kind  = kind;
            request.path  = path.string();
            request.plate = kind == "project_3mf" || kind == "presets" ? std::nullopt : std::optional<Workspace::PlateId>(plate);
            Workspace::ExportResult result;
            const auto done = workspace->export_file(request, result);
            if (!done.succeeded()) std::cout << name << ": " << done.message << std::endl;
            check(done.succeeded() && !result.files.empty() && result.bytes > 0, name);
            return result;
        };
        exported("gcode", folder / "plate.gcode", "outputs_export_gcode");
        std::string first_line;
        {
            std::ifstream written((folder / "plate.gcode").string());
            std::getline(written, first_line);
        }
        check(!first_line.empty() && first_line[0] == ';', "outputs_export_gcode_is_gcode");
        exported("sliced_3mf", folder / "plate.gcode.3mf", "outputs_export_sliced_3mf");
        exported("project_3mf", folder / "project.3mf", "outputs_export_project_3mf");
        exported("stl", folder / "plate.stl", "outputs_export_stl");
        const auto presets = exported("presets", folder, "outputs_export_presets");
        std::cout << "exported presets: " << presets.files.size() << " files" << std::endl;
        Workspace::ExportRequest again;
        again.kind = "gcode";
        again.path = (folder / "plate.gcode").string();
        check(!workspace->check_export(again).succeeded(), "outputs_export_refuses_to_replace_unasked");
        again.path = "relative.gcode";
        check(!workspace->check_export(again).succeeded(), "outputs_export_refuses_a_relative_path");
        again.path = (fs::path(data_dir()) / "stolen.gcode").string();
        again.overwrite = true;
        check(!workspace->check_export(again).succeeded(), "outputs_export_refuses_the_data_folder");
        boost::system::error_code removed;
        fs::remove_all(folder, removed);
        for (const auto& title : counter.titles) std::cout << "outputs dialog shown: " << title << std::endl;
        check(counter.shown == 0, "outputs_show_no_dialog");
    }

    // A slice stopped while it runs, through the slicing notification's own
    // Cancel.
    void verify_cancel(Workspace::PlateId plate, std::function<void()> then)
    {
        auto* workspace = installed_shell()->workspace();
        const Workspace::SettingsPatch finer{{{"layer_height", "0.12"}}};
        Workspace::SettingsPreview applied;
        workspace->apply_settings(finer, Workspace::settings_confirmation(workspace->preview_settings(finer)), applied);
        check(workspace->start_slice(plate, false).succeeded(), "outputs_cancel_slice_started");
        wait_until([] { return installed_shell()->workspace()->snapshot().slicing.running; }, "outputs_cancel_slice_running",
                   [self = shared_from_this(), then] {
                       bool stopped = false;
                       auto* workspace = installed_shell()->workspace();
                       const bool done = workspace->cancel_slice(stopped).succeeded();
                       std::cout << "cancel: succeeded " << done << " stopped " << stopped << " running after "
                                 << workspace->snapshot().slicing.running << std::endl;
                       self->check(done && stopped, "outputs_cancel_stops_the_slice");
                       self->wait_until([] { return !installed_shell()->workspace()->snapshot().slicing.running; },
                                        "outputs_cancel_slice_reported_stopped", then);
                   });
    }

    void slice_then(const char* name, Workspace::PlateId plate, std::function<void()> then)
    {
        auto* workspace = installed_shell()->workspace();
        check(workspace->start_slice(plate, false).succeeded(), std::string(name) + "_started");
        const auto sliced = [plate] {
            const auto snapshot = installed_shell()->workspace()->snapshot();
            return !snapshot.slicing.running &&
                   std::any_of(snapshot.plates.begin(), snapshot.plates.end(), [&](const auto& p) { return p.id == plate && p.sliced; });
        };
        // A result from before the change still reads as sliced until the run
        // starts; wait for the run first, then for its result.
        wait_until([sliced] { return !sliced(); }, std::string(name) + "_running",
                   [self = shared_from_this(), sliced, name, then] { self->wait_until(sliced, name, then); });
    }

    void finish_slice_checks(const std::function<void()>& then)
    {
        auto* workspace = installed_shell()->workspace();
        if (!m_slice_check_regions.empty())
            workspace->remove_regions(m_slice_check_regions);
        workspace->delete_items({Workspace::DeleteItem{Workspace::DeleteItem::Kind::Object, m_slice_check_object}});
        wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(m_slice_check_settings);
        then();
    }

    Workspace::ObjectId                  m_slice_check_object;
    std::vector<Workspace::RegionRecord> m_slice_check_regions;
    DynamicPrintConfig                   m_slice_check_settings;

    // Phase 6: record one real sliced plate as a deterministic build, then an
    // exported copy and completed physical print through the same page ->
    // Agent -> approval coordinator path, and confirm that changing the
    // manufacturing input makes the build's derived staleness visible.
    void agent_phase6_history()
    {
        const std::size_t objects_before = m_plater->model().objects.size();
        check(m_plater->duplicate_object(0) >= 0, "phase6_manufacturing_change_before_build");
        wait_until(
            [this, objects_before] { return m_plater->model().objects.size() > objects_before; },
            "phase6_manufacturing_change_applied", [self = shared_from_this()] {
                installed_shell()->status_row()->request_slice();
                self->wait_until(
                    [self] {
                        PartPlate* plate = self->m_plater->get_partplate_list().get_curr_plate();
                        return plate != nullptr && plate->is_slice_result_valid();
                    },
                    "phase6_active_plate_sliced", [self] { self->phase6_propose_build(); });
            });
    }

    void phase6_propose_build()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        const std::size_t activities_before = web_view.host().tools().activities().size();
        WebView::RunScript(web_view.webview(), "window.__jusprinTest && window.__jusprinTest.send('/build')");
        wait_until(
            [&web_view, activities_before] {
                const auto& activities = web_view.host().tools().activities();
                return activities.size() > activities_before && activities.back().tool == "record_build" &&
                       activities.back().state == Agent::ToolState::Pending;
            },
            "phase6_build_proposal_pending", [self = shared_from_this()] {
                AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
                const std::string action_id = web_view.host().tools().activities().back().action_id;
                WebView::RunScript(web_view.webview(),
                                   wxString::FromUTF8("window.__jusprinTest && window.__jusprinTest.decide('" + action_id +
                                                      "', 'approve')"));
                self->wait_until(
                    [self, &web_view, action_id] {
                        const Agent::ToolActivity* activity = web_view.host().tools().find(action_id);
                        return activity != nullptr && activity->state == Agent::ToolState::Succeeded &&
                               self->persistence().document().builds().size() == 1;
                    },
                    "phase6_build_recorded", [self] { self->phase6_propose_export(); });
            });
    }

    void phase6_propose_export()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        const std::size_t activities_before = web_view.host().tools().activities().size();
        WebView::RunScript(web_view.webview(), "window.__jusprinTest && window.__jusprinTest.send('/export')");
        wait_until(
            [&web_view, activities_before] {
                const auto& activities = web_view.host().tools().activities();
                return activities.size() > activities_before && activities.back().tool == "record_export_copy" &&
                       activities.back().state == Agent::ToolState::Pending;
            },
            "phase6_export_proposal_pending", [self = shared_from_this()] {
                AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
                const std::string action_id = web_view.host().tools().activities().back().action_id;
                WebView::RunScript(web_view.webview(),
                                   wxString::FromUTF8("window.__jusprinTest && window.__jusprinTest.decide('" + action_id +
                                                      "', 'approve')"));
                self->wait_until(
                    [self, &web_view, action_id] {
                        const Agent::ToolActivity* activity = web_view.host().tools().find(action_id);
                        return activity != nullptr && activity->state == Agent::ToolState::Succeeded &&
                               self->persistence().document().exported_copies().size() == 1;
                    },
                    "phase6_export_recorded", [self] { self->phase6_propose_print(); });
            });
    }

    void phase6_propose_print()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        const std::size_t activities_before = web_view.host().tools().activities().size();
        WebView::RunScript(web_view.webview(), "window.__jusprinTest && window.__jusprinTest.send('/print')");
        wait_until(
            [&web_view, activities_before] {
                const auto& activities = web_view.host().tools().activities();
                return activities.size() > activities_before && activities.back().tool == "record_physical_print" &&
                       activities.back().state == Agent::ToolState::Pending;
            },
            "phase6_print_proposal_pending", [self = shared_from_this()] {
                AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
                const std::string action_id = web_view.host().tools().activities().back().action_id;
                WebView::RunScript(web_view.webview(),
                                   wxString::FromUTF8("window.__jusprinTest && window.__jusprinTest.decide('" + action_id +
                                                      "', 'approve')"));
                self->wait_until(
                    [self, &web_view, action_id] {
                        const Agent::ToolActivity* activity = web_view.host().tools().find(action_id);
                        return activity != nullptr && activity->state == Agent::ToolState::Succeeded &&
                               self->persistence().document().physical_prints().size() == 1;
                    },
                    "phase6_physical_print_recorded", [self] { self->phase6_verify_records(); });
            });
    }

    void phase6_verify_records()
    {
        check(installed_shell()->status_row()->project_summary()
                  .Contains(wxString::FromUTF8("Prints \xC2\xB7 1")),
              "overflow_print_count_follows_the_ledger");
        const Agent::BuildRecord build = persistence().document().builds().front();
        const Agent::ExportedCopyRecord copy = persistence().document().exported_copies().front();
        const Agent::PhysicalPrintRecord print = persistence().document().physical_prints().front();
        check(build.manufacturing_input_hash.size() == 64 && build.output_hash.size() == 64,
              "phase6_build_has_sha256_provenance");
        check(build.plate_name == print.plate_name, "phase6_print_keeps_plate");
        check(!print.printer.empty() && !print.material.empty() && !print.started_at.empty() && !print.ended_at.empty(),
              "phase6_print_keeps_setup_and_times");
        check(print.outcome == "completed" && print.gcode_hash == build.output_hash,
              "phase6_print_keeps_outcome_and_gcode_hash");
        check(print.statistics.layer_count == 124 && copy.expected_output_hash == build.output_hash &&
                  copy.observed_output_hash == build.output_hash,
              "phase6_stats_and_export_checksum_survive");

        check(m_plater->duplicate_object(0) >= 0, "phase6_change_after_build");
        wait_until(
            [self = shared_from_this(), input_hash = build.manufacturing_input_hash, plate_index = build.plate_index] {
                const auto current = Agent::manufacturing_input_hash(self->installed_workspace_snapshot(), plate_index);
                return current && *current != input_hash;
            },
            "phase6_old_build_becomes_stale", [self = shared_from_this()] { self->agent_import_and_clean_share(); });
    }

    Workspace::WorkspaceSnapshot installed_workspace_snapshot() const
    {
        return installed_shell()->workspace()->snapshot();
    }

    void agent_import_and_clean_share()
    {
        const std::string identity_before = persistence().document().project_id();
        const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
        const std::vector<size_t> imported = m_plater->load_files(
            std::vector<std::string>{cube}, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence,
            false);
        check(imported.size() == 1, "import_adds_content");
        check(persistence().document().project_id() == identity_before, "import_keeps_project_identity");

        const std::string clean_path = (fs::temp_directory_path() / fs::unique_path("jusprin-clean-%%%%.3mf")).string();
        check(persistence().export_clean_copy(clean_path).succeeded(), "clean_copy_written");
        std::ifstream clean(clean_path, std::ios::binary);
        const std::string clean_bytes((std::istreambuf_iterator<char>(clean)), std::istreambuf_iterator<char>());
        check(clean_bytes.find("3D/3dmodel.model") != std::string::npos, "clean_copy_is_a_project_archive");
        check(clean_bytes.find("JusPrin/state.json") == std::string::npos, "clean_copy_has_no_conversation_state");

        std::cerr << "HARNESS BENCH saved_project_bytes=" << m_saved_project_bytes << " save_ms=" << m_save_ms << '\n';
        boost::system::error_code ec;
        fs::remove(m_saved_project_file, ec);
        fs::remove(clean_path, ec);
        after_agent();
    }

    void after_agent()
    {
        verify_resize();
        verify_project_replacement();
        verify_uninstall_restores_stock();
        finish();
    }

    void verify_resize()
    {
        auto* divider = wxWindow::FindWindowByName("Resize Agent panel", m_frame);
        auto* pane = installed_shell()->agent_pane();
        check(divider != nullptr && divider->IsShown(), "agent_resize_handle_shown");
        check(divider != nullptr && divider->GetSize().x == divider->FromDIP(8),
              "agent_resize_handle_matches_token_width");
        if (divider != nullptr) {
            auto drag = [divider](int delta_x) {
                const wxPoint start(divider->GetClientSize().x / 2, divider->GetClientSize().y / 2);
                wxMouseEvent down(wxEVT_LEFT_DOWN);
                down.SetPosition(start);
                down.SetEventObject(divider);
                divider->GetEventHandler()->ProcessEvent(down);
                wxMouseEvent motion(wxEVT_MOTION);
                motion.SetPosition(start + wxPoint(delta_x, 0));
                motion.SetEventObject(divider);
                divider->GetEventHandler()->ProcessEvent(motion);
                wxMouseEvent up(wxEVT_LEFT_UP);
                up.SetPosition(start + wxPoint(delta_x, 0));
                up.SetEventObject(divider);
                divider->GetEventHandler()->ProcessEvent(up);
                wxYield();
            };

            auto double_click = [divider] {
                const wxPoint start(divider->GetClientSize().x / 2, divider->GetClientSize().y / 2);
                for (wxEventType type : {wxEVT_LEFT_DOWN, wxEVT_LEFT_UP, wxEVT_LEFT_DCLICK, wxEVT_LEFT_UP}) {
                    wxMouseEvent event(type);
                    event.SetPosition(start);
                    event.SetEventObject(divider);
                    divider->GetEventHandler()->ProcessEvent(event);
                }
                wxYield();
            };
            auto* toggle = dynamic_cast<HeaderButton*>(
                wxWindow::FindWindowByName("Agent panel", installed_shell()->status_row()));
            auto press_toggle = [&toggle] {
                if (!toggle) return;
                wxMouseEvent press(wxEVT_LEFT_DOWN), release(wxEVT_LEFT_UP);
                toggle->ProcessWindowEvent(press);
                toggle->ProcessWindowEvent(release);
                wxYield();
            };
            auto workspace_width = [this] {
                return m_plater->canvas3D()->get_wxglcanvas()->GetSize().GetWidth();
            };

            const int original_pane_width = pane->GetSize().x;
            drag(-divider->FromDIP(120));
            check(pane->GetSize().x > original_pane_width, "agent_panel_mouse_drag_grows_width");

            // However far a drag is pushed, it only sizes the pane: it stops
            // at the minimum and never closes it, so aiming for a narrow pane
            // cannot take the conversation off the screen by accident.
            drag(divider->FromDIP(2000));
            check(pane->GetSize().x == pane->FromDIP(320), "agent_panel_drag_stops_at_minimum_width");
            check(!installed_shell()->is_agent_pane_collapsed() && pane->IsShown() && divider->IsShown(),
                  "agent_panel_drag_never_closes_the_pane");

            drag(-divider->FromDIP(2000));
            const int expected_max = std::max(pane->FromDIP(320),
                m_frame->GetClientSize().x - pane->FromDIP(320) - divider->GetSize().x);
            check(pane->GetSize().x == expected_max, "agent_panel_drag_uses_available_window_width");
            check(workspace_width() > 200, "agent_panel_drag_preserves_workspace_width");

            drag(pane->GetSize().x - original_pane_width);
            check(pane->GetSize().x == original_pane_width, "agent_panel_width_can_be_restored");

            check(toggle != nullptr && toggle->IsShown(), "agent_panel_toggle_shown");
            const int open_workspace_width = workspace_width();

            double_click();
            check(installed_shell()->is_agent_pane_collapsed(), "agent_panel_divider_double_click_collapses");
            check(!pane->IsShown() && !divider->IsShown(), "agent_panel_collapse_hides_pane_and_divider");
            check(workspace_width() > open_workspace_width, "agent_panel_collapse_widens_workspace");
            check(pane->web_view().host().mcp() != nullptr, "agent_panel_collapse_keeps_the_runtime");

            press_toggle();
            check(!installed_shell()->is_agent_pane_collapsed() && pane->IsShown() && divider->IsShown(),
                  "agent_panel_toggle_reopens_the_pane");
            check(pane->GetSize().x == pane->FromDIP(320), "agent_panel_reopens_at_a_usable_width");

            press_toggle();
            check(installed_shell()->is_agent_pane_collapsed(), "agent_panel_toggle_closes_the_pane");
            press_toggle();
            check(!installed_shell()->is_agent_pane_collapsed(), "agent_panel_toggle_reopens_again");

            // The toggle's two states are one glyph with its right-hand column
            // filled or empty. No assertion on state can see which was drawn,
            // so the button paints itself into a file the way the chip does.
            if (toggle != nullptr) {
                const char* artifact_dir = std::getenv("JUSPRIN_ARTIFACT_DIR");
                const fs::path out = artifact_dir != nullptr ? fs::path(artifact_dir)
                                                             : fs::path(data_dir()) / "agent-toggle";
                fs::create_directories(out);
                auto shoot = [&](const std::string& name) {
                    const wxBitmap bitmap = toggle->snapshot();
                    const std::string file = (out / (name + ".png")).string();
                    const bool ok = bitmap.IsOk() && bitmap.GetWidth() > 0 &&
                                    bitmap.ConvertToImage().SaveFile(wxString::FromUTF8(file), wxBITMAP_TYPE_PNG);
                    check(ok, "agent_panel_toggle_renders_" + name);
                    if (ok) std::cout << "HARNESS ARTIFACT " << name << " " << file << std::endl;
                    return bitmap.IsOk() ? bitmap.ConvertToImage() : wxImage();
                };
                const wxImage open_glyph = shoot("agent-toggle-open");
                press_toggle();
                const wxImage closed_glyph = shoot("agent-toggle-closed");
                press_toggle();
                const bool differ = open_glyph.IsOk() && closed_glyph.IsOk() &&
                                    open_glyph.GetWidth() == closed_glyph.GetWidth() &&
                                    open_glyph.GetHeight() == closed_glyph.GetHeight() &&
                                    std::memcmp(open_glyph.GetData(), closed_glyph.GetData(),
                                                std::size_t(open_glyph.GetWidth()) * open_glyph.GetHeight() * 3) != 0;
                check(differ, "agent_panel_toggle_draws_its_two_states_differently");
                check(!installed_shell()->is_agent_pane_collapsed(), "agent_panel_open_after_capture");
            }

            drag(-divider->FromDIP(120));
            check(pane->GetSize().x > original_pane_width, "agent_panel_still_resizable_after_collapse");
            drag(pane->GetSize().x - original_pane_width);
            check(pane->GetSize().x == original_pane_width, "agent_panel_width_restored_after_collapse");

            // Both checks below need a pane that is not sitting at its own
            // minimum: only a pane wide enough to have width taken from it
            // exercises the width policy at all.
            const wxSize whole = m_frame->GetSize();
            m_frame->SetSize(m_frame->FromDIP(760), whole.y);
            m_frame->Layout();
            wxYield();

            // A drag that runs past the edge of a narrow window asks for a
            // width the pane cannot have. That width must not be banked: the
            // pane would take it, at the workspace's expense, as soon as the
            // window grew enough to allow it.
            drag(-divider->FromDIP(3000));
            const int narrow_max = pane->GetSize().x;
            m_frame->SetSize(m_frame->FromDIP(1400), whole.y);
            m_frame->Layout();
            wxYield();
            check(pane->GetSize().x == narrow_max,
                  "agent_panel_over_drag_is_not_banked_for_a_larger_window");

            // Shrinking the window while the pane is wider than the room left
            // for it re-runs the width policy on every step. No Layout() call
            // in the loop, on purpose: the point is what the frame's own size
            // handlers leave behind, and an extra layout against the settled
            // client size would repair a stale one before it could be seen.
            drag(-divider->FromDIP(3000));
            for (int width : {1200, 1000, 860, 760}) {
                m_frame->SetSize(m_frame->FromDIP(width), whole.y);
                wxYield();
                verify_agent_pane_tiling("shrinking_to_" + std::to_string(width) + "_dip");
            }
            for (int width : {860, 1100, 1400}) {
                m_frame->SetSize(m_frame->FromDIP(width), whole.y);
                wxYield();
                verify_agent_pane_tiling("growing_to_" + std::to_string(width) + "_dip");
            }
            m_frame->SetSize(whole);
            m_frame->Layout();
            wxYield();
            drag(pane->GetSize().x - original_pane_width);
        }

        const wxSize original = m_frame->GetSize();
        verify_agent_pane_tiling("before_resize");
        m_frame->SetSize(original + wxSize(120, 80));
        m_frame->Layout();
        StatusRow* row = installed_shell()->status_row();
        check(row->IsShown() && row->GetSize().GetWidth() > 0, "status_row_survives_resize");
        check(m_plater->canvas3D()->get_wxglcanvas()->GetSize().GetWidth() > 200, "canvas_survives_resize");
        verify_agent_pane_tiling("after_grow");
        verify_header_layout();
        m_frame->SetSize(m_frame->FromDIP(900),original.y);
        m_frame->Layout();
        // The single setup chip became a two-half printer/spool chip; both
        // halves must exist, since each anchors its own menu.
        auto* setup = wxWindow::FindWindowByName("Printer",row) && wxWindow::FindWindowByName("Spool",row)
                          ? wxWindow::FindWindowByName("Printer and spool",row) : nullptr;
        setup->SetLabel(wxString('W',180));
        row->SendSizeEvent();
        verify_header_layout();
        row->refresh();
        m_frame->SetSize(original);
        m_frame->Layout();
    }

    // Workspace, divider and pane must tile the frame's client width exactly.
    // Each one on its own can look right while the three together do not: a
    // layout run against a stale width sizes some of them for the previous
    // width, so they overlap and the pane covers part of the workspace.
    void verify_agent_pane_tiling(const std::string& name)
    {
        auto* pane = installed_shell()->agent_pane();
        auto* divider = wxWindow::FindWindowByName("Resize Agent panel", m_frame);
        if (pane == nullptr || divider == nullptr || installed_shell()->is_agent_pane_collapsed())
            return;
        const int client = m_frame->GetClientSize().x;
        const wxRect workspace = m_notebook->GetRect();
        const wxRect bar = divider->GetRect();
        const wxRect agent = pane->GetRect();
        const bool tiled = workspace.x == 0 && workspace.GetWidth() > 0 &&
                           workspace.GetRight() + 1 == bar.x &&
                           bar.GetRight() + 1 == agent.x &&
                           agent.GetRight() + 1 == client;
        check(tiled, "agent_panel_columns_tile_the_client_" + name);
        if (!tiled)
            std::cerr << "HARNESS DETAIL client=" << client << " workspace=" << workspace.x << "+"
                      << workspace.GetWidth() << " divider=" << bar.x << "+" << bar.GetWidth()
                      << " pane=" << agent.x << "+" << agent.GetWidth() << '\n';
    }

    void verify_header_layout()
    {
        auto* row = installed_shell()->status_row();
        auto* home = wxWindow::FindWindowByName("Home navigation",row);
        // The single setup chip became a two-half printer/spool chip; both
        // halves must exist, since each anchors its own menu.
        auto* setup = wxWindow::FindWindowByName("Printer",row) && wxWindow::FindWindowByName("Spool",row)
                          ? wxWindow::FindWindowByName("Printer and spool",row) : nullptr;
        auto* action = wxWindow::FindWindowByName("Next print action",row);
        auto* arrow = wxWindow::FindWindowByName("Print actions",row);
        auto* more = wxWindow::FindWindowByName("Project actions",row);
        check(home && setup && action && arrow && more,"header_has_all_five_controls");
        if (!home || !setup || !action || !arrow || !more) return;
        check(row->GetSize().y == row->FromDIP(56),"header_matches_56_dip_design");
        check(home->GetPosition().x == row->FromDIP(16),"header_home_left_margin");
        check(home->GetRect().GetRight() < setup->GetPosition().x && setup->GetRect().GetRight() < action->GetPosition().x,
              "header_home_setup_actions_order_without_overlap");
        // The reducer returns no menu items for a single unsliced plate, and the
        // chevron half is hidden in that state, so the joined-halves geometry
        // only applies while both halves are laid out.
        if (arrow->IsShown()) {
            check(action->GetPosition().x+action->GetSize().x == arrow->GetPosition().x,"header_split_halves_joined");
            check(arrow->GetSize().y == action->GetSize().y,"header_split_halves_equal_height");
        } else {
            check(action->GetRect().GetRight() < more->GetPosition().x,"header_lone_action_precedes_overflow");
        }
        check(action->GetSize().y == row->FromDIP(34),"header_action_height");
        check(more->GetPosition().x+more->GetSize().x == row->GetSize().x-row->FromDIP(16),"header_overflow_right_aligned");
        check(!setup->GetLabel().Contains("Plate ") && !setup->GetLabel().Contains("@"),"header_setup_compact_no_plate_or_raw_suffix");
        check(!status_row_labels(row).Contains(wxString::FromUTF8("Prints \xC2\xB7")),"header_has_no_standalone_print_count");
    }

    // The em of a font in pixels. wx has no portable accessor for it: on MSW
    // GetPixelSize() reads LOGFONT::lfHeight, the em at the screen DPI the
    // font was built for; on macOS and GTK it measures a rendered "g", which
    // is the line height, but there a point is a DIP
    // (wxHAS_DPI_INDEPENDENT_PIXELS), so the em is the point size: as is on
    // macOS, and at Pango's 96 dpi logical resolution on GTK.
    static int font_em_pixels(const wxFont& font)
    {
#if defined(__WXMSW__)
        return font.GetPixelSize().y;
#elif defined(__APPLE__)
        return wxRound(font.GetFractionalPointSize());
#else
        return wxRound(font.GetFractionalPointSize() * 96.0 / 72.0);
#endif
    }

    // The type roles are DIP sizes and every role font must render its token
    // as its em. Windows once drew the body role at 15 px because
    // Label::sysFont truncates 14 * 4 / 5 to 11 pt, which is why filament
    // names truncated sooner in the spool picker there than on macOS. The
    // shell resolves its theme through brand_theme(), so that is checked.
    void verify_type_roles_render_at_token_size()
    {
        const ShellTheme* theme = brand_theme();
        check(theme != nullptr, "shell_theme_loaded");
        if (theme == nullptr) return;
        const std::pair<TextRole, const char*> roles[] = {
            {TextRole::Body, "body"}, {TextRole::Label, "label"}, {TextRole::Metadata, "metadata"}};
        for (const auto& [role, name] : roles) {
            const int         token = theme->type_style(role).size;
            const std::string suffix = std::string("_em_is_") + std::to_string(token) + "_dip";
            check(font_em_pixels(theme->font(role)) == m_frame->FromDIP(token), std::string("font_") + name + suffix);
        }
    }

    void verify_project_replacement()
    {
        check(m_plater->new_project(true, true) != wxID_CANCEL, "replacement_project");
        check(installed_shell() != nullptr && installed_shell()->is_installed(), "shell_survives_project_replacement");
        check(!m_notebook->GetBtnsListCtrl()->IsShown(), "tab_strip_still_hidden_after_replacement");
        check(!wxGetApp().sidebar().IsShown(), "sidebar_still_hidden_after_replacement");
    }

    void verify_uninstall_restores_stock()
    {
        // A printer connection can leave an agent the slicing profile would not
        // pick. Detaching the shell hands the choice back to the profile.
        NetworkAgent* agent = wxGetApp().getAgent();
        const std::string profile_agent = agent->get_printer_agent()->get_agent_info().id;
        agent->set_printer_agent(NetworkAgentFactory::create_printer_agent_by_id(
            JUSPRIN_FAKE_BAMBU_AGENT_ID, agent->get_cloud_agent(BBL_CLOUD_PROVIDER), Slic3r::data_dir()));
        check(profile_agent != JUSPRIN_FAKE_BAMBU_AGENT_ID &&
                  agent->get_printer_agent()->get_agent_info().id == JUSPRIN_FAKE_BAMBU_AGENT_ID,
              "detach_fixture_installs_an_agent_the_profile_does_not_pick");
        detach_shell();
        check(agent->get_printer_agent()->get_agent_info().id == profile_agent, "detach_restores_the_profile_driven_agent");
        check(installed_shell() == nullptr, "shell_detached");
        check(m_notebook->GetBtnsListCtrl()->IsShown(), "tab_strip_restored");
        check(m_plater->is_sidebar_available(), "sidebar_available_restored");
        check(!m_plater->get_view3D_canvas3D()->legacy_overlays_hidden(),
              "prepare_legacy_overlays_restored");
        m_frame->Layout();
        check(m_plater->canvas3D()->get_wxglcanvas()->GetSize().GetWidth() > 200, "stock_canvas_usable_after_restore");
    }

    // Polls through a one-shot wxTimer rather than a self-reposting
    // CallAfter: a pending-event spin keeps wxApp's pending queue non-empty,
    // which starves any nested YieldFor on the stack (wx's WKWebView
    // AddScriptMessageHandler runs script through one) and deadlocks the
    // WebView setup this harness is waiting on.
    // each_tick runs before every re-test of the condition. It exists for
    // states that only the page can report: the tick asks the page, the page
    // answers over the bridge, and the condition sees the answer.
    void wait_until(std::function<bool()> condition,
                    std::string           name,
                    std::function<void()> then,
                    std::function<void()> each_tick = {})
    {
        m_wait_condition = std::move(condition);
        m_wait_name      = std::move(name);
        m_wait_then      = std::move(then);
        m_wait_each_tick = std::move(each_tick);
        poll_wait();
    }

    // A settle condition rather than a delay: post_init has been entered, the
    // frame is out of post_init's Freeze/Thaw, and the loop has delivered a
    // further wxEVT_IDLE since -- which it only does once nothing is pending:
    // no queued page change, no CallAfter, no timer already due. The watcher
    // is bound on the app after GUI_App's own idle handler, so wx runs it
    // first in the same event; arming only after post_initialized() is seen
    // is what keeps the idle that ran post_init from counting.
    void wait_until_settled(std::string name, std::function<void()> then)
    {
        if (!m_idle_watch_bound) {
            m_app.Bind(wxEVT_IDLE, [weak = std::weak_ptr<Scenario>(shared_from_this())](wxIdleEvent& event) {
                if (auto self = weak.lock(); self && self->m_idle_armed)
                    self->m_idle_seen = true;
                event.Skip();
            });
            m_idle_watch_bound = true;
        }
        m_idle_armed = false;
        m_idle_seen  = false;
        wait_until([this] {
            if (!m_app.post_initialized() || m_frame->IsFrozen())
                return false;
            if (!m_idle_armed) {
                m_idle_armed = true;
                return false;
            }
            return m_idle_seen;
        }, std::move(name), std::move(then));
    }

    void poll_wait()
    {
        if (m_state->stop || !m_wait_condition)
            return;
        if (m_wait_condition()) {
            check(true, m_wait_name);
            auto then        = std::move(m_wait_then);
            m_wait_condition = nullptr;
            m_wait_then      = nullptr;
            m_wait_each_tick = nullptr;
            then();
            return;
        }
        if (m_wait_each_tick)
            m_wait_each_tick();
        if (++m_wait_ticks % 500 == 0) {
            PartPlate* plate = m_plater->get_partplate_list().get_curr_plate();
            std::cerr << "HARNESS WAIT " << m_wait_name << " preview=" << m_plater->is_preview_shown()
                      << " plate=" << m_plater->get_partplate_list().get_curr_plate_index()
                      << " slice_valid=" << (plate != nullptr && plate->is_slice_result_valid()) << '\n';
        }
        if (std::chrono::steady_clock::now() >= m_state->deadline) {
            check(false, m_wait_name);
            fail(m_wait_name + " did not become true before the deadline");
            return;
        }
        m_poll_timer.StartOnce(10);
    }

    void fail(const std::string& message)
    {
        std::cerr << "HARNESS ERROR " << message << '\n';
        ++m_failures;
        finish();
    }

    void finish()
    {
        if (m_finished)
            return;
        m_finished = true;
        const int result = m_failures == 0 ? 0 : 1;
        std::cerr << "HARNESS RESULT " << (result == 0 ? "PASS" : "FAIL") << " failures=" << m_failures << '\n';
        m_state->result = result;
        m_state->stop = true;
        m_poll_timer.Stop();
        if (m_frame == nullptr) {
            m_app.ExitMainLoop();
            return;
        }
        // Leave the way the application leaves. A forced close runs
        // MainFrame::shutdown() -- backup callback cleared, canvas handlers
        // unbound and volumes released, background threads stopped,
        // GUI_App::set_closing, the window hidden -- and then Destroy()s the
        // frame inside the main loop, so wx leaves the loop on its own once
        // the last top-level window is gone and GUI_App::OnExit() runs with
        // nothing left to tear down.
        //
        // ExitMainLoop() skipped all of that: OnExit() deleted the device
        // manager and agent first, and wxAppBase::CleanUp() then deleted the
        // still-shown MainFrame from inside ~wxInitializer, where any
        // exception is std::terminate -- a 0xC0000409 exit on Windows with
        // nothing printed. Deferred so nothing on the current stack keeps
        // using a frame that has already been shut down.
        m_app.CallAfter([frame = m_frame] { frame->Close(true); });
        // Safety net: a stray top-level window would keep the loop alive.
        m_exit_watchdog.StartOnce(30000);
    }

    GUI_App&                      m_app;
    std::shared_ptr<HarnessState> m_state;
    Plater*                       m_plater{nullptr};
    MainFrame*                    m_frame{nullptr};
    Notebook*                     m_notebook{nullptr};
    int                           m_failures{0};
    bool                          m_finished{false};
    std::uint64_t                 m_wait_ticks{0};
    nlohmann::json                m_settings_original, m_settings_patch;
    std::size_t                   m_objects_before_tool{0};
    std::size_t                   m_live_copies_before{0};
    std::string                   m_saved_project_id;
    std::string                   m_saved_project_file;
    std::string                   m_live_action_id;
    bool                          m_live_rejection_done{false};
    std::unique_ptr<JusPrinTest::NativeMcpClient> m_mcp_client;
    std::size_t                   m_saved_project_bytes{0};
    double                        m_save_ms{0.0};
    std::string                   m_header_setup_printer;
    std::string                   m_header_setup_restore_printer;
    Selection::IndicesList        m_tool_strip_selection;
    bool                          m_capture_typing{true};

    wxEvtHandler          m_poll_handler;
    wxTimer               m_poll_timer{&m_poll_handler};
    wxEvtHandler          m_exit_handler;
    wxTimer               m_exit_watchdog{&m_exit_handler};
    std::function<bool()> m_wait_condition;
    std::string           m_wait_name;
    std::function<void()> m_wait_then;
    std::function<void()> m_wait_each_tick;
    bool                  m_idle_watch_bound{false};
    bool                  m_idle_armed{false};
    bool                  m_idle_seen{false};
};

// The app ships no stand-in Agent, so each mode's Agent is installed here
// through the host's own set_agent, the call setup uses when a key verifies.
void install_harness_agent(HarnessState::Mode mode)
{
    ShellController* shell = installed_shell();
    if (shell == nullptr)
        return; // stock mode: the shell is disabled, so there is no Agent panel
    Agent::AgentHost& host = shell->agent_pane()->web_view().host();
    switch (mode) {
    case HarnessState::Mode::LiveAgentUnavailable:
    case HarnessState::Mode::ManualUnconfigured:
        return;
    case HarnessState::Mode::LiveAgent:
    case HarnessState::Mode::ManualLiveAgent: {
        // Without a key the live checks report the missing service themselves.
        const char* key = std::getenv("OPENAI_API_KEY");
        if (key == nullptr || *key == '\0')
            return;
        // The printer panel builds its own Agent from the app configuration,
        // so hands-on printer setup needs the key there too: this run's
        // throwaway data directory, as --printer-live does. A std::string,
        // or AppConfig::set picks its bool overload.
        if (mode == HarnessState::Mode::ManualLiveAgent)
            wxGetApp().app_config->set("jusprin_agent", "openai_api_key", std::string(key));
        Agent::OpenAIResponsesConfig config;
        config.api_key = key;
        config.usage_listener = [](const Agent::AgentUsage& usage) {
            // cached_input_tokens is the evidence the tool-loading decision
            // rests on; report it on every request, not only when it is
            // non-zero, so a chat that stops caching is visible in the log.
            std::cerr << "JUSPRIN LIVE USAGE provider=openai input_tokens=" << usage.input
                      << " cached_input_tokens=" << usage.cached_input << " output_tokens=" << usage.output
                      << " total_tokens=" << usage.total << '\n';
        };
        host.set_agent(std::make_unique<Agent::OpenAIResponsesAgent>(std::move(config), Agent::make_openai_http_transport()),
                       Agent::AgentAvailability::Ready);
        return;
    }
    default:
        host.set_agent(std::make_unique<Agent::DeterministicMockAgent>(), Agent::AgentAvailability::Ready);
        return;
    }
}

void start_when_ready(GUI_App& app, const std::shared_ptr<HarnessState>& state)
{
    if (state->stop)
        return;
    if (std::chrono::steady_clock::now() >= state->deadline) {
        std::cerr << "HARNESS ERROR application did not become ready before timeout\n";
        state->result = 1;
        state->stop = true;
        app.ExitMainLoop();
        return;
    }
    if (app.mainframe != nullptr && app.plater() != nullptr) {
#ifdef __APPLE__
        if (state->dark_appearance) set_harness_appearance(*state->dark_appearance);
#endif
        install_harness_agent(state->mode);
        if (state->mode == HarnessState::Mode::Manual || state->mode == HarnessState::Mode::ManualLiveAgent ||
            state->mode == HarnessState::Mode::ManualUnconfigured)
            return;
        auto scenario = std::make_shared<Scenario>(app, state);
        state->runner = scenario;
        scenario->start();
        return;
    }
    app.CallAfter([&app, state] { start_when_ready(app, state); });
}

} // namespace
} // namespace Slic3r::GUI::JusPrin

namespace {

// The argument the parent sent, as the bytes it sent.
//
// run_mcp_setup_command hands wxExecute a wide argv built with
// wxString::FromUTF8, so Windows receives the argument intact -- but the CRT
// then builds this process's narrow argv by converting that command line to
// the ANSI code page, where the fixture literal's CJK has no representation
// and is replaced before main is even entered. Nothing the parent does can
// prevent that. Windows keeps the real command line in UTF-16, so read the
// argument from there and re-encode it as the UTF-8 the parent will search
// the output for. Off Windows the narrow argv already carries those bytes.
std::string utf8_argument(int index, char** argv)
{
#ifdef _WIN32
    int count = 0;
    wchar_t** wide = ::CommandLineToArgvW(::GetCommandLineW(), &count);
    if (wide == nullptr) return argv[index];
    std::string text;
    if (index < count) {
        const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, wide[index], -1, nullptr, 0, nullptr, nullptr);
        if (bytes > 1) {
            text.resize(std::size_t(bytes) - 1);
            ::WideCharToMultiByte(CP_UTF8, 0, wide[index], -1, text.data(), bytes, nullptr, nullptr);
        }
    }
    ::LocalFree(wide);
    return text;
#else
    return argv[index];
#endif
}

} // namespace

int main(int argc, char** argv)
{
    // A real subprocess for setup tests, with no GUI or external configuration.
    if (argc == 4 && std::string(argv[1]) == "--setup-child") {
        const std::string mode(argv[2]);
        if (mode == "timeout" || mode == "ignore-term") {
            if (mode == "ignore-term") std::signal(SIGTERM, SIG_IGN);
            std::cout << "fixture pid " << wxGetProcessId() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(45));
            return 0; // The timeout test must terminate us before this fallback.
        }
        if (mode == "delayed") std::this_thread::sleep_for(std::chrono::milliseconds(800));
        if (mode == "large") std::cout << std::string(256 * 1024, 'x');
        std::cout << utf8_argument(3, argv) << '\n';
        std::cerr << "fixture stderr\n";
        return mode == "failure" ? 7 : 0;
    }
    // Registered before any function-local static is constructed, so those
    // statics' destructors run before this marker and namespace-scope
    // statics' destructors run after it.
    std::atexit([] {
        std::fputs("HARNESS ATEXIT REACHED\n", stderr);
        std::fflush(stderr);
    });
    using namespace Slic3r;
    using namespace Slic3r::GUI;
    using namespace Slic3r::GUI::JusPrin;

    const fs::path original_directory = fs::current_path();
    fs::path data_directory = fs::temp_directory_path() / fs::unique_path("jusprin-shell-%%%%-%%%%-%%%%");

    auto state = std::make_shared<HarnessState>();
    std::vector<char*> gui_arguments;
    gui_arguments.reserve(static_cast<std::size_t>(argc));
    gui_arguments.emplace_back(argv[0]);
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--dark-ui" || argument == "--light-ui") {
#if defined(__APPLE__) || defined(_WIN32)
            state->dark_appearance = argument == "--dark-ui";
#else
            std::cerr << "Appearance overrides are supported only by the macOS and Windows harnesses\n";
            return 2;
#endif
        }
        else if (argument == "--stock")
            state->mode = HarnessState::Mode::Stock;
        else if (argument == "--manual")
            state->mode = HarnessState::Mode::Manual;
        else if (argument == "--manual-live-agent")
            state->mode = HarnessState::Mode::ManualLiveAgent;
        else if (argument == "--manual-unconfigured")
            state->mode = HarnessState::Mode::ManualUnconfigured;
        else if (argument == "--manual-tool-strip")
            state->mode = HarnessState::Mode::ManualToolStrip;
        else if (argument == "--slice-all-cold")
            state->mode = HarnessState::Mode::SliceAllCold;
        else if (argument == "--live-agent")
            state->mode = HarnessState::Mode::LiveAgent;
        else if (argument == "--mcp")
            state->mode = HarnessState::Mode::Mcp;
        else if (argument == "--mcp-setup")
            state->mode = HarnessState::Mode::McpSetup;
        else if (argument == "--manual-mcp" || argument == "--header-visual") {
            if (++index == argc) {
                std::cerr << "--manual-mcp requires a dedicated fixture directory\n";
                return 2;
            }
            state->mode = HarnessState::Mode::ManualMcp;
            state->header_visual = argument == "--header-visual";
            data_directory = fs::absolute(argv[index]);
        }
        else if (argument == "--mcp-bridge") {
            state->mode = HarnessState::Mode::Mcp;
            state->mcp_bridge = true;
        }
        else if (argument == "--live-agent-unavailable")
            state->mode = HarnessState::Mode::LiveAgentUnavailable;
        else if (argument == "--printer-setup")
            state->mode = HarnessState::Mode::PrinterSetup;
        else if (argument == "--printer-live")
            state->mode = HarnessState::Mode::PrinterLive;
        else if (argument == "--printer-connect-capture") {
            if (++index == argc) {
                std::cerr << "--printer-connect-capture requires an output directory\n";
                return 2;
            }
            state->mode            = HarnessState::Mode::PrinterLive;
            state->connect_capture = true;
            state->capture_dir     = fs::absolute(argv[index]);
        }
        else if (argument == "--recomputing-capture") {
            if (++index == argc) {
                std::cerr << "--recomputing-capture requires an output directory\n";
                return 2;
            }
            state->mode = HarnessState::Mode::RecomputingCapture;
            state->capture_dir = fs::absolute(argv[index]);
        }
        else if (argument == "--timeline-capture") {
            if (++index == argc) {
                std::cerr << "--timeline-capture requires an output directory\n";
                return 2;
            }
            state->mode = HarnessState::Mode::TimelineCapture;
            state->capture_dir = fs::absolute(argv[index]);
        }
        else if (argument == "--tool-strip-capture") {
            if (++index == argc) {
                std::cerr << "--tool-strip-capture requires an output directory\n";
                return 2;
            }
            state->mode = HarnessState::Mode::ToolStripCapture;
            state->capture_dir = fs::absolute(argv[index]);
        }
        else
            gui_arguments.emplace_back(argv[index]);
    }
#ifdef __APPLE__
    if (state->dark_appearance) set_harness_appearance(*state->dark_appearance);
#endif
    fs::create_directories(data_directory / "log");
    if (state->mode == HarnessState::Mode::LiveAgentUnavailable) {
        // The setup key check in this scenario must exercise the real host,
        // page, and HTTP transport without reaching a real provider. A closed
        // local port gives a genuine connection failure to surface.
        wxSetEnv("JUSPRIN_OPENAI_ENDPOINT", "http://127.0.0.1:1/v1/responses");
    }

    {
        std::ifstream base(std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/jusprin/harness.conf");
        std::string   config((std::istreambuf_iterator<char>(base)), std::istreambuf_iterator<char>());
        if (state->mode == HarnessState::Mode::Stock) {
            const std::string anchor = "\"language\": \"en_US\",";
            config.replace(config.find(anchor), anchor.size(), anchor + "\n    \"jusprin_shell\": \"0\",");
        }
#ifdef _WIN32
        // On Windows GUI_App::dark_mode() honours dark_color_mode alone, and
        // AppConfig::set_defaults writes "0" when the key is absent, so the
        // system theme is never consulted. Seeding the key here reaches
        // Update_dark_mode_flag and NppDarkMode::InitDarkMode before any
        // window exists, the same path the Preferences toggle takes.
        if (state->dark_appearance) {
            const std::string anchor = "\"language\": \"en_US\",";
            config.replace(config.find(anchor), anchor.size(),
                           anchor + "\n    \"dark_color_mode\": \"" + (*state->dark_appearance ? "1" : "0") + "\",");
        }
#endif
        if (state->mode == HarnessState::Mode::LiveAgent ||
            state->mode == HarnessState::Mode::ManualLiveAgent || state->mode == HarnessState::Mode::PrinterLive ||
            state->mode == HarnessState::Mode::LiveAgentUnavailable) {
            // The base config has the Agent off: no provider, no key, no
            // consent, exactly what a fresh install looks like.
            const std::string from = "\"jusprin_agent\": {\n    \"enabled\": false\n  }";
            const bool live_enabled = state->mode == HarnessState::Mode::LiveAgent ||
                                      state->mode == HarnessState::Mode::ManualLiveAgent ||
                                      state->mode == HarnessState::Mode::PrinterLive;
            const std::string consent = live_enabled ? "true" : "false";
            const std::string to = "\"jusprin_agent\": {\n    \"cloud_consent\": " + consent +
                                   ",\n    \"enabled\": true,\n    \"model\": \"gpt-5.4-mini\",\n    \"provider\": \"openai\"\n  }";
            const std::size_t pos = config.find(from);
            if (pos != std::string::npos)
                config.replace(pos, from.size(), to);
        }
        std::ofstream out((data_directory / (std::string(SLIC3R_APP_KEY) + ".conf")).string());
        out << config;
    }

    const fs::path resources = fs::path(JUSPRIN_SOURCE_DIR) / "resources";
    set_resources_dir(resources.string());
    set_var_dir((resources / "images").string());
    set_local_dir((resources / "i18n").string());
    set_sys_shapes_dir((resources / "shapes").string());
    set_custom_gcodes_dir((resources / "custom_gcodes").string());
    set_data_dir(data_directory.string());
    set_temporary_dir(data_directory.string());
    save_main_thread_id();

    std::thread installer([state] {
        while (!state->stop && std::chrono::steady_clock::now() < state->deadline) {
            if (auto* app = dynamic_cast<GUI_App*>(wxApp::GetInstance())) {
                app->CallAfter([app, state] { start_when_ready(*app, state); });
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        state->result = 1;
        state->stop = true;
    });

    GUI_InitParams params;
    params.argc = static_cast<int>(gui_arguments.size());
    params.argv = gui_arguments.data();
    const int gui_result = GUI_Run(params);
    std::cerr << "HARNESS GUI_RUN RETURNED " << gui_result << '\n';
    state->stop = true;
    installer.join();

    if (state->mode == HarnessState::Mode::Mcp) {
        const bool removed = !fs::exists(data_directory / "jusprin" / "mcp.json");
        std::cerr << "HARNESS CHECK mcp_discovery_removed_after_app_shutdown " << (removed ? "PASS" : "FAIL") << '\n';
        if (!removed) state->result = 1;
        if (state->bridge) {
            state->bridge->request(JusPrinTest::request("tools/call", {{"name", "workspace_inspect"}}));
            const bool responded = JusPrinTest::wait_for([&] { return state->bridge->done(); }, [] {});
            const auto messages = state->bridge->messages();
            const bool offline = responded && !messages.empty() && messages.back().contains("result") &&
                messages.back()["result"]["structuredContent"]["error"]["code"] == "workspace_unavailable";
            std::cerr << "HARNESS CHECK mcp_bridge_survives_app_shutdown_and_reports_offline " << (offline ? "PASS" : "FAIL") << '\n';
            const bool clean = state->bridge->shutdown();
            std::cerr << "HARNESS CHECK mcp_bridge_eof_exit_zero " << (clean ? "PASS" : "FAIL") << '\n';
            if (!offline || !clean) state->result = 1;
        }
    }

    fs::current_path(original_directory);
    if (state->result == 0) {
        // The boost::log sink keeps log/debug_*.log.0 open until static
        // destruction, so on Windows this cannot delete everything, and an
        // uncaught filesystem_error here would end a PASS run with
        // 0xE06D7363. Report instead of throwing.
        boost::system::error_code error;
        fs::remove_all(data_directory, error);
        if (error)
            std::cerr << "HARNESS WARNING data dir not fully removed: " << error.message() << " ("
                      << data_directory.string() << ")\n";
    } else
        std::cerr << "HARNESS DATA DIR kept for inspection: " << data_directory.string() << '\n';
    int exit_code = state->result;
    if (state->mode == HarnessState::Mode::Manual || state->mode == HarnessState::Mode::ManualLiveAgent ||
        state->mode == HarnessState::Mode::ManualUnconfigured || state->mode == HarnessState::Mode::ManualMcp ||
        state->mode == HarnessState::Mode::ManualToolStrip)
        exit_code = gui_result;
    else if (state->result < 0)
        exit_code = gui_result == 0 ? 1 : gui_result;
    // Static destructors run after this line; a crash without a later
    // ATEXIT line is in a function-local static (the 3mf backup manager,
    // the shell slot, ...), one after it is in a namespace-scope static.
    std::cerr << "HARNESS MAIN RETURNING " << exit_code << '\n';
    return exit_code;
}
