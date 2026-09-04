// Runs the complete native application and verifies the production JusPrin
// shell through Phase 6: installation, Prepare/Slice/Check print, the typed
// Agent bridge and tools, persistence/Revert, manufacturing history, resize
// and project replacement, fallback, and restoration of stock presentation.
//
// Modes:
//   --dark-ui / --light-ui  macOS process-only appearance for visual checks
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

#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/libslic3r.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_Init.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentWebView.hpp"
#include "slic3r/GUI/JusPrin/Mcp/McpRuntime.hpp"
#include "../agent/mcp_test_client.hpp"
#include "mcp_stdio_client.hpp"
#include "slic3r/GUI/JusPrin/Shell/AgentPane.hpp"
#include "slic3r/GUI/JusPrin/Shell/McpConnectionDialog.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellController.hpp"
#include "slic3r/GUI/JusPrin/Shell/StatusRow.hpp"
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
#include <wx/glcanvas.h>
#include <wx/dialog.h>
#include <wx/process.h>
#include <wx/stdpaths.h>
#include <wx/timer.h>

#include <boost/filesystem.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace fs = boost::filesystem;

#ifdef __APPLE__
void set_harness_appearance(bool dark);
#endif

namespace Slic3r::GUI::JusPrin {
namespace {

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
        LiveAgentUnavailable
    };

    std::atomic<int>  result{-1};
    std::atomic<bool> stop{false};
    std::shared_ptr<void> runner;
    // Phase 4 added real project saves, reopen, and checkpoint exports on
    // top of two full slices; 300 s was regularly exhausted mid-flow. The
    // warm Slice-all scenario adds up to two more plate slices.
    std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::now() + std::chrono::seconds(900)};
    Mode mode{Mode::Shell};
    bool mcp_bridge{false};
    std::optional<bool> dark_appearance;
    std::shared_ptr<JusPrinTest::StdioClient> bridge;
};

Notebook* find_notebook(wxWindow* root)
{
    if (auto* notebook = dynamic_cast<Notebook*>(root))
        return notebook;
    for (wxWindow* child : root->GetChildren())
        if (Notebook* notebook = find_notebook(child))
            return notebook;
    return nullptr;
}

class Scenario final : public std::enable_shared_from_this<Scenario>
{
public:
    Scenario(GUI_App& app, std::shared_ptr<HarnessState> state) : m_app(app), m_state(std::move(state))
    {
        m_poll_handler.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { poll_wait(); });
    }

    void start()
    {
        try {
            m_plater = m_app.plater();
            m_frame  = m_app.mainframe;
            check(m_plater != nullptr && m_frame != nullptr, "application_ready");
            m_notebook = find_notebook(m_frame);
            check(m_notebook != nullptr, "notebook_found");
            if (m_plater == nullptr || m_frame == nullptr || m_notebook == nullptr) {
                fail("application widgets missing");
                return;
            }
            if (m_state->mode == HarnessState::Mode::Stock) {
                verify_stock_mode();
                finish();
                return;
            }
            verify_shell_installed();
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
                    self->m_plater->set_sidebar_available(true);
                    self->m_plater->collapse_sidebar(false);
                    self->check(self->m_plater->sidebar().IsShown(), "mcp_fixture_native_sidebar_visible");
                    std::cerr << "HARNESS MCP FIXTURE READY failures=" << self->m_failures
                              << " discovery=" << data_dir() << "/jusprin/mcp.json\n";
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
            if (m_state->mode == HarnessState::Mode::SliceAllCold) {
                begin_slice_all_cold();
                return;
            }
            verify_canvas_interaction();
            begin_slice_check();
        } catch (const std::exception& error) {
            fail(std::string("exception: ") + error.what());
        } catch (...) {
            fail("unknown exception");
        }
    }

private:
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
        wxString text;
        if (auto* label = dynamic_cast<wxStaticText*>(window))
            text += label->GetLabel() + "\n";
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
        check(shell->status_row() != nullptr && shell->status_row()->IsShown(), "status_row_shown");
        check(status_row_labels(shell->status_row()).Contains(wxString::FromUTF8("Prints \xC2\xB7 0")),
              "status_row_shows_empty_print_count");
        check(shell->agent_pane() != nullptr && shell->agent_pane()->IsShown(), "agent_pane_shown");
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
        check(installed_shell()->is_installed() && installed_shell()->status_row()->IsShown(),
              "shell_survives_rejected_install");
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
        installed_shell()->status_row()->request_slice();
        wait_until([this] { return m_plater->is_preview_shown(); }, "slice_switches_to_preview",
                   [self = shared_from_this()] {
                       self->wait_until(
                           [self] {
                               PartPlate* plate = self->m_plater->get_partplate_list().get_curr_plate();
                               return plate != nullptr && plate->is_slice_result_valid();
                           },
                           "slice_completes", [self] { self->after_slice(); });
                   });
    }

    void after_slice()
    {
        // The user flow under test: with a valid slice, Check print shows the
        // real Preview, and Back to Prepare returns.
        installed_shell()->status_row()->request_check_print();
        wait_until([this] { return m_plater->is_preview_shown(); }, "check_print_shows_preview",
                   [self = shared_from_this()] {
                       installed_shell()->status_row()->request_prepare();
                       self->wait_until([self] { return !self->m_plater->is_preview_shown(); }, "returns_to_prepare",
                                        [self] { self->after_return(); });
                   });
    }

    void after_return()
    {
        begin_slice_all_warm();
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
        wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_SLICE_ALL));
        wait_until([this] { return all_nonempty_plates_sliced(); }, "slice_all_completes_all_plates",
                   [self = shared_from_this()] {
                       // Restore the deterministic pre-scenario state the
                       // later phases assume: first plate active, Prepare
                       // shown. on_action_slice_all switches the shown panel
                       // to Preview directly without moving the Notebook tab,
                       // so the tab still reads Prepare; re-align the tab to
                       // Preview first or the Prepare selection is a no-op
                       // that fires no page-changed event.
                       self->m_plater->select_plate(0);
                       installed_shell()->status_row()->request_check_print();
                       installed_shell()->status_row()->request_prepare();
                       self->wait_until([self] { return !self->m_plater->is_preview_shown(); },
                                        "slice_all_returns_to_prepare", [self] { self->verify_agent_bridge(); });
                   });
    }

    // Opt-in --slice-all-cold: same dispatch, but before the Preview canvas
    // has ever rendered. See the mode comment at the top of this file.
    void begin_slice_all_cold()
    {
        check(!m_plater->is_preview_shown(), "cold_starts_in_prepare");
        check(m_plater->get_partplate_list().get_nonempty_plate_list().size() > 1, "cold_fixture_has_multiple_plates");
        wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_SLICE_ALL));
        wait_until([this] { return all_nonempty_plates_sliced(); }, "cold_slice_all_completes_all_plates",
                   [self = shared_from_this()] {
                       self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "cold_teardown_project");
                       self->finish();
                   });
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
            "window.__jusprinTest && window.__jusprinTest.send('Duplicate the currently selected object now. Use the "
            "duplicate_object tool with the exact sessionId and objectId from the authoritative workspace context.');");
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
        check(activity.tool == "duplicate_object", "live_agent_proposed_typed_duplicate");
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
                    self->check(self->m_plater->model().objects.size() == self->m_objects_before_tool,
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
        check(m_plater->model().objects.size() == m_objects_before_tool + 1, "live_agent_mutated_real_orca_model_once");
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
        const auto inspect_id = web_view.host().tools().propose({"inspect_selection", "{}"}, "live-selection-proof").action_id;
        web_view.host().pump_tools();
        const auto* inspected = web_view.host().tools().find(inspect_id);
        check(inspected && inspected->state == Agent::ToolState::Succeeded &&
              nlohmann::json::parse(inspected->result_json)["selection"].size() == 1, "live_agent_shared_selection_result");
        check(installed_shell()->workspace()->undo().succeeded(), "live_agent_native_undo_executes");
        check(m_plater->model().objects.size() == m_objects_before_tool, "live_agent_native_undo_restores_object_count");
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
                else {
                    self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "live_agent_teardown_project");
                    self->finish();
                }
            }, [id, stage] { click_rendered_tool_decision(id, stage != 0); });
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

    void prepare_mcp_slice(std::function<void()> next)
    {
        installed_shell()->status_row()->request_slice();
        wait_until([this] {
            const auto* plate = m_plater->get_partplate_list().get_curr_plate();
            return plate && plate->is_slice_result_valid();
        }, "mcp_fixture_has_real_slice", [self = shared_from_this(), next] {
            installed_shell()->status_row()->request_prepare();
            self->wait_until([self] { return !self->m_plater->is_preview_shown(); }, "mcp_fixture_prepare", next);
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
                self->check(tools.size() == 5 && tools.back()["name"] == "workspace_inspect", "mcp_real_registry_catalog");
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
                        "mcp_settings_invalidate_real_slice", [self] { self->mcp_mutation(2); });
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
                const auto& conversation = web_view.host().conversation();
                return conversation.size() >= 2 && conversation.back().state == Agent::MessageState::Complete;
            },
            "agent_reply_streams_to_completion", [self = shared_from_this()] { self->agent_verify_reply_and_context(); });
    }

    void agent_verify_reply_and_context()
    {
        AgentWebView&     web_view = installed_shell()->agent_pane()->web_view();
        Agent::AgentHost& host     = web_view.host();

        const auto& conversation = host.conversation();
        check(conversation.size() == 2, "agent_conversation_has_exchange");
        check(conversation.front().role == Agent::MessageRole::User, "agent_user_message_recorded");
        check(conversation.back().role == Agent::MessageRole::Assistant, "agent_reply_recorded");
        // The reply must describe the authoritative fixture, not canned text:
        // the two-plate fixture and its active plate contents appear in it.
        check(conversation.back().text.find("2 plates") != std::string::npos, "agent_reply_describes_fixture_plates");
        check(conversation.back().text.find("is active with") != std::string::npos, "agent_reply_describes_active_plate");

        // A native selection change must push fresh context over the bridge.
        const std::uint64_t sent_before = host.messages_sent();
        check(m_plater->select_object(1), "agent_native_selection_change");
        wait_until([&host, sent_before] { return host.messages_sent() > sent_before; },
                   "agent_context_pushed_on_native_selection",
                   [self = shared_from_this()] { self->agent_verify_reload(); });
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
    void agent_tool_propose()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        check(m_plater->select_object(0), "tool_target_selected");
        m_objects_before_tool = m_plater->model().objects.size();
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
                self->check(self->m_plater->model().objects.size() == self->m_objects_before_tool,
                            "tool_rejection_changes_nothing");
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
        check(m_plater->model().objects.size() == m_objects_before_tool + 1, "tool_duplicate_visible_in_model");
        check(m_plater->canvas3D()->get_volumes_count() >= 3, "tool_duplicate_visible_on_canvas");
        check(m_plater->can_undo_project(), "tool_change_is_undoable");
        check(m_plater->undo_project(), "tool_undo_through_orca");
        check(m_plater->model().objects.size() == m_objects_before_tool, "tool_undo_removes_duplicate");
        check(m_plater->redo_project(), "tool_redo_through_orca");
        check(m_plater->model().objects.size() == m_objects_before_tool + 1, "tool_redo_restores_duplicate");
        agent_conversations();
    }

    // Phase 4: conversations, project-owned persistence, save/reopen,
    // Revert here, import identity, and the clean-sharing copy — all against
    // the real application and real 3MF archives.
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
                        const auto& conversation = web_view.host().conversation();
                        return conversation.size() >= 2 && conversation.back().state == Agent::MessageState::Complete;
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
        // auxiliary dir (with state.json and checkpoints) is included.
        const auto save_started = std::chrono::steady_clock::now();
        check(m_plater->export_3mf(boost::filesystem::path(m_saved_project_file),
                                   SaveStrategy::SplitModel | SaveStrategy::ShareMesh | SaveStrategy::Silence) >= 0,
              "project_saved_with_state");
        m_save_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - save_started).count();
        std::ifstream saved(m_saved_project_file, std::ios::binary);
        const std::string saved_bytes((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
        m_saved_project_bytes = saved_bytes.size();
        check(saved_bytes.find("JusPrin/state.json") != std::string::npos, "saved_archive_contains_state_json");
        check(saved_bytes.find(".snapshot") != std::string::npos, "saved_archive_contains_checkpoints");

        check(m_plater->new_project(true, true) != wxID_CANCEL, "phase4_new_project");
        wait_until(
            [this] {
                return persistence().document().has_identity() && persistence().document().project_id() != m_saved_project_id;
            },
            "new_project_starts_new_identity", [self = shared_from_this()] {
                self->m_plater->load_project(wxString::FromUTF8(self->m_saved_project_file), "<silence>");
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
                        self->check(!self->persistence().document().revisions().empty(),
                                    "saved_revisions_survive_reopen");
                        self->agent_phase6_history();
                    });
            });
    }

    // Phase 6: record one real sliced plate as a deterministic build, then an
    // exported copy and completed physical print through the same page ->
    // Agent -> approval coordinator path. Change manufacturing input, confirm
    // derived staleness, and Revert past the print: editable history goes away
    // while the factual print ledger remains.
    void agent_phase6_history()
    {
        m_phase6_objects_before  = m_plater->model().objects.size();
        m_phase6_target_revision = persistence().document().current_revision_id();
        check(!m_phase6_target_revision.empty(), "phase6_target_revision_known");
        check(m_plater->duplicate_object(0) >= 0, "phase6_manufacturing_change_before_build");
        wait_until(
            [this] { return persistence().document().current_revision_id() != m_phase6_target_revision; },
            "phase6_source_revision_captured", [self = shared_from_this()] {
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
                    "phase6_physical_print_recorded", [self] { self->phase6_verify_and_revert(); });
            });
    }

    void phase6_verify_and_revert()
    {
        check(status_row_labels(installed_shell()->status_row())
                  .Contains(wxString::FromUTF8("Prints \xC2\xB7 1")),
              "status_row_print_count_follows_the_ledger");
        const Agent::BuildRecord build = persistence().document().builds().front();
        const Agent::ExportedCopyRecord copy = persistence().document().exported_copies().front();
        const Agent::PhysicalPrintRecord print = persistence().document().physical_prints().front();
        check(build.manufacturing_input_hash.size() == 64 && build.output_hash.size() == 64,
              "phase6_build_has_sha256_provenance");
        check(build.revision_id == print.revision_id && build.plate_name == print.plate_name,
              "phase6_print_keeps_revision_and_plate");
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
            "phase6_old_build_becomes_stale", [self = shared_from_this()] {
                AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
                WebView::RunScript(web_view.webview(),
                                   wxString::FromUTF8("window.__jusprinTest && window.__jusprinTest.revert('" +
                                                      self->m_phase6_target_revision + "')"));
                self->wait_until(
                    [self] { return self->persistence().document().current_revision_id() == self->m_phase6_target_revision; },
                    "phase6_revert_completed", [self] { self->phase6_verify_retention(); });
            });
    }

    Workspace::WorkspaceSnapshot installed_workspace_snapshot() const
    {
        return installed_shell()->workspace()->snapshot();
    }

    void phase6_verify_retention()
    {
        check(m_plater->model().objects.size() == m_phase6_objects_before,
              "phase6_revert_restores_native_state");
        check(!m_plater->can_redo_project(), "phase6_revert_leaves_no_redo");
        check(persistence().document().builds().empty() && persistence().document().exported_copies().empty(),
              "phase6_revert_removes_later_editable_history");
        const auto prints = persistence().document().physical_prints();
        check(prints.size() == 1, "phase6_physical_print_survives_revert");
        check(!prints.empty() && !persistence().document().find_revision(prints.front().revision_id).has_value(),
              "phase6_print_source_timeline_removed");
        check(!prints.empty() && prints.front().outcome == "completed" && prints.front().gcode_hash.size() == 64 &&
                  prints.front().statistics.layer_count == 124,
              "phase6_surviving_print_facts_exact");
        agent_import_and_clean_share();
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
        check(clean_bytes.find(".snapshot") == std::string::npos, "clean_copy_has_no_checkpoints");

        const Agent::ProjectPersistence::CheckpointStats& stats = persistence().stats();
        std::cerr << "HARNESS BENCH checkpoint_captures=" << stats.captures
                  << " capture_failures=" << stats.capture_failures
                  << " total_snapshot_bytes=" << stats.total_snapshot_bytes
                  << " last_capture_ms=" << stats.last_capture_ms
                  << " last_restore_ms=" << stats.last_restore_ms
                  << " saved_project_bytes=" << m_saved_project_bytes
                  << " save_ms=" << m_save_ms << '\n';
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
        const wxSize original = m_frame->GetSize();
        m_frame->SetSize(original + wxSize(120, 80));
        m_frame->Layout();
        StatusRow* row = installed_shell()->status_row();
        check(row->IsShown() && row->GetSize().GetWidth() > 0, "status_row_survives_resize");
        check(m_plater->canvas3D()->get_wxglcanvas()->GetSize().GetWidth() > 200, "canvas_survives_resize");
        m_frame->SetSize(original);
        m_frame->Layout();
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
        detach_shell();
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
        m_app.ExitMainLoop();
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
    std::size_t                   m_phase6_objects_before{0};
    std::string                   m_phase6_target_revision;
    std::string                   m_saved_project_id;
    std::string                   m_saved_project_file;
    std::string                   m_live_action_id;
    bool                          m_live_rejection_done{false};
    std::unique_ptr<JusPrinTest::NativeMcpClient> m_mcp_client;
    std::size_t                   m_saved_project_bytes{0};
    double                        m_save_ms{0.0};

    wxEvtHandler          m_poll_handler;
    wxTimer               m_poll_timer{&m_poll_handler};
    std::function<bool()> m_wait_condition;
    std::string           m_wait_name;
    std::function<void()> m_wait_then;
    std::function<void()> m_wait_each_tick;
};

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
        std::cout << argv[3] << '\n';
        std::cerr << "fixture stderr\n";
        return mode == "failure" ? 7 : 0;
    }
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
#ifdef __APPLE__
            state->dark_appearance = argument == "--dark-ui";
#else
            std::cerr << "Appearance overrides are supported only by the macOS harness\n";
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
        else if (argument == "--slice-all-cold")
            state->mode = HarnessState::Mode::SliceAllCold;
        else if (argument == "--live-agent")
            state->mode = HarnessState::Mode::LiveAgent;
        else if (argument == "--mcp")
            state->mode = HarnessState::Mode::Mcp;
        else if (argument == "--mcp-setup")
            state->mode = HarnessState::Mode::McpSetup;
        else if (argument == "--manual-mcp") {
            if (++index == argc) {
                std::cerr << "--manual-mcp requires a dedicated fixture directory\n";
                return 2;
            }
            state->mode = HarnessState::Mode::ManualMcp;
            data_directory = fs::absolute(argv[index]);
        }
        else if (argument == "--mcp-bridge") {
            state->mode = HarnessState::Mode::Mcp;
            state->mcp_bridge = true;
        }
        else if (argument == "--live-agent-unavailable")
            state->mode = HarnessState::Mode::LiveAgentUnavailable;
        else
            gui_arguments.emplace_back(argv[index]);
    }
#ifdef __APPLE__
    if (state->dark_appearance) set_harness_appearance(*state->dark_appearance);
#endif
    fs::create_directories(data_directory / "log");
    if (state->mode == HarnessState::Mode::LiveAgent || state->mode == HarnessState::Mode::ManualLiveAgent)
        wxSetEnv("JUSPRIN_AGENT_RECORD_USAGE", "1");
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
        if (state->mode == HarnessState::Mode::ManualUnconfigured) {
            // No provider, no key, no consent: exactly what a fresh install
            // looks like before anyone sets an Agent up.
            const std::string from = "\"jusprin_agent\": {\n    \"enabled\": true,\n    \"provider\": \"mock\"\n  }";
            const std::string to   = "\"jusprin_agent\": {\n    \"enabled\": false\n  }";
            const std::size_t pos  = config.find(from);
            if (pos != std::string::npos)
                config.replace(pos, from.size(), to);
        }
        if (state->mode == HarnessState::Mode::LiveAgent ||
            state->mode == HarnessState::Mode::ManualLiveAgent ||
            state->mode == HarnessState::Mode::LiveAgentUnavailable) {
            const std::string from = "\"jusprin_agent\": {\n    \"enabled\": true,\n    \"provider\": \"mock\"\n  }";
            const bool live_enabled = state->mode == HarnessState::Mode::LiveAgent ||
                                      state->mode == HarnessState::Mode::ManualLiveAgent;
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
    if (state->result == 0)
        fs::remove_all(data_directory);
    else
        std::cerr << "HARNESS DATA DIR kept for inspection: " << data_directory.string() << '\n';
    if (state->mode == HarnessState::Mode::Manual || state->mode == HarnessState::Mode::ManualLiveAgent ||
        state->mode == HarnessState::Mode::ManualUnconfigured || state->mode == HarnessState::Mode::ManualMcp)
        return gui_result;
    if (state->result < 0)
        return gui_result == 0 ? 1 : gui_result;
    return state->result;
}
