// Runs the complete native application and verifies the production JusPrin
// shell through Phase 6: installation, Prepare/Slice/Check print, the typed
// Agent bridge and tools, persistence/Revert, manufacturing history, resize
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
#include "slic3r/GUI/JusPrin/Shell/McpSetupCommand.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellController.hpp"
#include "slic3r/GUI/JusPrin/Shell/PrinterSpoolChip.hpp"
#include "slic3r/GUI/JusPrin/Shell/SetupCommands.hpp"
#include "slic3r/GUI/JusPrin/Shell/StatusRow.hpp"
#include "slic3r/GUI/JusPrin/Shell/HeaderControls.hpp"
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
#include <wx/scrolwin.h>
#include <wx/process.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

#include <boost/filesystem.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
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
        RecomputingCapture
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
    bool header_visual{false};
    bool review_visual{false};
    std::optional<bool> dark_appearance;
    fs::path capture_dir;
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
                    if (self->m_state->review_visual) {
                        auto* row = installed_shell()->status_row();
                        // Both of these do nothing at all when the slice
                        // identity is invalid: the report attaches to a slice
                        // that cannot be matched, and request_check_print
                        // returns before it navigates. Naming the identity
                        // separates "the fixture had nothing to report on" from
                        // "the report was made and the panel still did not
                        // appear", which the mode's silence used to conflate.
                        self->check(row->slice_identity().valid(), "review_visual_has_a_slice_to_report_on");
                        installed_shell()->workspace()->slice_reviews()->report(row->slice_identity(),row->slice_identity(),
                            {"Test fixture: inspect the opening and the support contact before printing."});
                        row->request_check_print();
                        self->check(self->m_notebook->GetSelection() == MainFrame::tpPreview,
                                    "review_visual_reaches_preview");
                    } else if (self->m_state->header_visual) {
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
                    // that result. m_workspace_status (plate label, return
                    // button, review panel) is only shown on Prepare/Preview,
                    // so its label is the operator's tell made into a check.
                    self->wait_until_settled("mcp_fixture_settled", [self] {
                        const bool preview = self->m_state->review_visual;
                        self->check(self->m_notebook->GetSelection() == (preview ? MainFrame::tpPreview : MainFrame::tp3DEditor),
                                    "mcp_fixture_tab_survives_settle");
                        self->check(self->m_plater->is_preview_shown() == preview, "mcp_fixture_view_survives_settle");
                        auto* label = wxWindow::FindWindowByName("Active plate status", self->m_frame);
                        self->check(label && label->IsShownOnScreen(), "mcp_fixture_workspace_status_visible");
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
        check(shell->status_row() != nullptr && shell->status_row()->IsShown(), "status_row_shown");
        check(shell->status_row()->project_summary().Contains(wxString::FromUTF8("Prints \xC2\xB7 0")),
              "overflow_summary_shows_empty_print_count");
        verify_header_layout();
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
                       auto* label = wxWindow::FindWindowByName("Active plate status",self->m_frame);
                       self->check(label && label->GetLabel().Contains("Slicing") && label->GetLabel().Contains("%"),"plate_label_shows_slicing_progress");
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
        check(wxWindow::FindWindowByName("Active plate status", m_frame) != nullptr, "wireframe_plate_label_exists");
        check(!m_plater->is_preview_shown(), "slice_stays_in_prepare");
        check(primary_print_action(row->action_state()).primary.action == PrintAction::Print, "unflagged_slice_offers_print");
        auto* primary = wxWindow::FindWindowByName("Next print action", row);
        check(primary && primary->GetLabel().StartsWith("Print"), "slice_completion_updates_rendered_button");
        auto& tools = installed_shell()->agent_pane()->web_view().host().tools();
        const auto identity = row->slice_identity();
        const auto activity = tools.propose({"report_slice_review", nlohmann::json{{"sessionId", std::to_string(identity.session)},
            {"plateId", std::to_string(identity.plate)}, {"sliceResultId", std::to_string(identity.result)},
            {"findings", {"Inspect the opening in Preview"}}}.dump()}, "header-review-fixture");
        for (int i = 0; i < 10 && !Agent::tool_state_terminal(tools.find(activity.action_id)->state); ++i) tools.pump();
        check(tools.find(activity.action_id)->state == Agent::ToolState::Succeeded, "agent_report_reaches_native_header");
        check(primary_print_action(row->action_state()).primary.action == PrintAction::CheckPrint, "finding_offers_check_print");
        check(primary && primary->GetLabel() == "Check print", "report_updates_rendered_button");
        const int original_plate = m_plater->get_partplate_list().get_curr_plate_index();
        m_plater->select_plate(original_plate == 0 ? 1 : 0);
        check(primary_print_action(row->action_state()).primary.action == PrintAction::Slice, "other_unsliced_plate_offers_slice");
        m_plater->select_plate(original_plate);
        check(primary_print_action(row->action_state()).primary.action == PrintAction::CheckPrint, "return_to_flagged_plate_preserves_review");
        row->request_slice();
        check(!m_plater->is_background_process_slicing(), "valid_slice_cannot_reslice");
        // The user flow under test: with a valid slice, Check print shows the
        // real Preview, and Back to Prepare returns.
        installed_shell()->status_row()->request_check_print();
        wait_until([this] { return m_plater->is_preview_shown() &&
            primary_print_action(installed_shell()->status_row()->action_state()).primary.action == PrintAction::Print; },
                   "check_preview_acknowledges_exact_slice",
                   [self = shared_from_this()] {
                       auto* report = wxWindow::FindWindowByName("Slice review findings", self->m_frame);
                       self->check(report && report->IsShownOnScreen(), "wireframe_findings_visible_before_acknowledgement");
                       const auto actions = primary_print_action(installed_shell()->status_row()->action_state());
                       self->check(std::none_of(actions.menu.begin(), actions.menu.end(), [](const auto& item) {
                           return item.action == PrintAction::Prepare;
                       }), "wireframe_print_menu_has_no_navigation");
                       self->verify_long_review();
                   });
    }

    void verify_long_review()
    {
        auto* row = installed_shell()->status_row();
        std::vector<std::string> findings(16,"Inspect the support contact on this surface before printing. This finding must remain available while reviewing the actual sliced toolpaths.");
        installed_shell()->workspace()->slice_reviews()->report(row->slice_identity(),row->slice_identity(),findings);
        wait_until([this] {
            auto* report = dynamic_cast<wxScrolledWindow*>(wxWindow::FindWindowByName("Slice review findings",m_frame));
            return report && report->GetVirtualSize().y > report->GetClientSize().y;
        },"long_review_is_scrollable",[self=shared_from_this()] {
            self->check(installed_shell()->status_row()->action_state().needs_review,"unseen_findings_keep_check_print");
            auto* report = dynamic_cast<wxScrolledWindow*>(wxWindow::FindWindowByName("Slice review findings",self->m_frame));
            report->Bind(wxEVT_PAINT,[weak=std::weak_ptr<Scenario>(self)](wxPaintEvent& e) {
                if (auto owner = weak.lock()) ++owner->m_report_paints;
                e.Skip();
            });
            report->Scroll(0,10000);
            report->Refresh(); report->Update();
            self->m_app.CallAfter([self] {
                self->check(installed_shell()->status_row()->action_state().needs_review,"skipping_findings_does_not_acknowledge");
                self->read_report_lines(0);
            });
        });
    }

    void read_report_lines(int position)
    {
        auto* report = dynamic_cast<wxScrolledWindow*>(wxWindow::FindWindowByName("Slice review findings",m_frame));
        const int before = m_report_paints;
        report->Scroll(0,position);
        report->Refresh(); report->Update();
        wait_until([this,before] { return m_report_paints > before; },"review_scroll_position_painted",
            [self=shared_from_this(),position] {
                auto* report = dynamic_cast<wxScrolledWindow*>(wxWindow::FindWindowByName("Slice review findings",self->m_frame));
                if (position*report->FromDIP(16) < report->GetVirtualSize().y) self->read_report_lines(position+1);
                else self->after_report_read();
            });
    }

    void after_report_read()
    {
        wait_until([] { return !installed_shell()->status_row()->action_state().needs_review; },
            "displaying_all_findings_acknowledges_report",[self=shared_from_this()] {
                installed_shell()->status_row()->request_prepare();
                self->wait_until([self] { return !self->m_plater->is_preview_shown(); },"returns_to_prepare",[self] { self->after_return(); });
            });
    }

    void after_return()
    {
        auto* report = wxWindow::FindWindowByName("Slice review findings",m_frame);
        check(report && !report->IsShownOnScreen(),"review_report_hidden_in_prepare");
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
        auto dismiss = [](HeaderMenu* menu) {
            if (!menu) return;
            wxKeyEvent escape(wxEVT_CHAR_HOOK); escape.m_keyCode = WXK_ESCAPE;
            menu->GetEventHandler()->ProcessEvent(escape);
        };
        wxKeyEvent open_key(wxEVT_KEY_DOWN); open_key.m_keyCode = WXK_RETURN;
        chevron->ProcessWindowEvent(open_key);
        auto* key_menu = visible_header_menu();
        check(key_menu && key_menu->selected_item(),"header_keyboard_menu_starts_on_first_row");
        dismiss(key_menu);

        wxMouseEvent press(wxEVT_LEFT_DOWN), release(wxEVT_LEFT_UP);
        chevron->ProcessWindowEvent(press);
        chevron->ProcessWindowEvent(release);
        auto* mouse_menu = visible_header_menu();
        check(mouse_menu && !mouse_menu->selected_item(),"header_mouse_menu_starts_unhighlighted");
        if (mouse_menu) {
            wxKeyEvent down(wxEVT_CHAR); down.m_keyCode = WXK_DOWN;
            mouse_menu->GetEventHandler()->ProcessEvent(down);
            check(mouse_menu->selected_item(),"header_mouse_menu_arrow_selects_first_row");
        }
        dismiss(mouse_menu);
    }

    void verify_header_menus()
    {
        auto* row = installed_shell()->status_row();
        row->show_action_menu();
        auto* menu = visible_header_menu();
        check(menu && menu->IsShown(),"header_action_menu_visible");
        auto* check_item = menu ? wxWindow::FindWindowByName("Check print",menu) : nullptr;
        check(check_item && wxWindow::FindWindowByName(ui_name("Print all plates…"),menu),"header_menu_has_contextual_actions");
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
                installed_shell()->status_row()->request_home();
                self->wait_until([self] { return self->m_notebook->GetSelection() == MainFrame::tp3DEditor && !self->m_plater->is_preview_shown(); },
                    "header_home_returns_to_prepare",[self] {
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
            installed_shell()->status_row()->open_printer_menu();
            choose_header_item(ui_name("Printer settings…"),
                [self=shared_from_this(),type] { self->verify_header_setup_open(type); });
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
        check(wxWindow::FindWindowByName("Nozzle", menu) != nullptr, "printer_menu_has_nozzle_row");
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

        const wxString before = row->spool_text();
        check(row->select_spool(second.id), "select_spool_applies_a_remembered_spool");
        check(row->spool_text() == wxString::FromUTF8(second.name), "chip_shows_the_swapped_spool");
        check(row->spool_text() != before, "the_chip_actually_changed");
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
        check(row->spool_text() == wxString::FromUTF8(crossed.name), "chip_shows_the_cross_preset_spool");
    }

    void verify_header_setup_open(Preset::Type type)
    {
        const std::string kind = type == Preset::TYPE_PRINTER ? "printer" : "filament";
        wait_until([] { return wxGetApp().params_dialog()->IsShown(); },"header_setup_opens_" + kind + "_editor",
            [self=shared_from_this(),type,kind] {
                self->check(wxGetApp().params_dialog()->panel()->get_current_tab() == wxGetApp().get_tab(type),
                            "header_setup_selects_" + kind + "_tab");
                wxGetApp().params_dialog()->Close();
                if (type == Preset::TYPE_PRINTER) self->verify_header_setup(Preset::TYPE_FILAMENT);
                else self->verify_header_overflow();
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

    // What is on the screen, not what a widget would draw: the card is
    // WebView2 content, which no snapshot() can paint offscreen. Needs the
    // frame on a visible desktop; at 100% scaling GetScreenRect() and the
    // screen DC share pixels (see handoff item 4 before trusting 150%/200%).
    void write_screen_capture(const wxRect& rect, const std::string& name)
    {
        fs::create_directories(m_state->capture_dir);
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
                self->check(tools.size() == 6 && tools.back()["name"] == "workspace_inspect", "mcp_real_registry_catalog");
                self->mcp_request(JusPrinTest::request("tools/call", {{"name", "workspace_inspect"}}));
                self->mcp_wait([self] {
                    const auto result = self->mcp_result()["structuredContent"];
                    self->check(result["plateCount"] == 2 && result["objectCount"] == self->m_objects_before_tool, "mcp_real_workspace_snapshot");
                    self->mcp_slice_review(result);
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

    void mcp_slice_review(const nlohmann::json& snapshot)
    {
        nlohmann::json arguments;
        for (const auto& plate : snapshot["plates"]["items"])
            if (plate["active"] == true) arguments = {{"sessionId", snapshot["sessionId"]}, {"plateId", plate["plateId"]},
                {"sliceResultId", plate["sliceResultId"]}, {"findings", {"Fixture: inspect opening"}}};
        check(!arguments.is_null() && arguments["sliceResultId"] != "", "mcp_exposes_completed_slice_identity");
        mcp_request(JusPrinTest::request("tools/call", {{"name", "report_slice_review"}, {"arguments", arguments}}));
        mcp_wait([self = shared_from_this()] {
            self->check(self->mcp_result()["structuredContent"]["reported"] == true, "mcp_slice_report_succeeds");
            auto* row = installed_shell()->status_row();
            self->check(primary_print_action(row->action_state()).primary.action == PrintAction::CheckPrint, "mcp_report_updates_header");
            row->request_check_print();
            self->wait_until([self] { return self->m_plater->is_preview_shown() &&
                primary_print_action(installed_shell()->status_row()->action_state()).primary.action == PrintAction::Print; },
                "mcp_finding_can_be_reviewed", [self] {
                    installed_shell()->status_row()->request_prepare();
                    self->mcp_settings_reads();
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
        check(installed_shell()->status_row()->project_summary()
                  .Contains(wxString::FromUTF8("Prints \xC2\xB7 1")),
              "overflow_print_count_follows_the_ledger");
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
    int                           m_report_paints{0};
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
        else if (argument == "--slice-all-cold")
            state->mode = HarnessState::Mode::SliceAllCold;
        else if (argument == "--live-agent")
            state->mode = HarnessState::Mode::LiveAgent;
        else if (argument == "--mcp")
            state->mode = HarnessState::Mode::Mcp;
        else if (argument == "--mcp-setup")
            state->mode = HarnessState::Mode::McpSetup;
        else if (argument == "--manual-mcp" || argument == "--header-visual" || argument == "--review-visual") {
            if (++index == argc) {
                std::cerr << "--manual-mcp requires a dedicated fixture directory\n";
                return 2;
            }
            state->mode = HarnessState::Mode::ManualMcp;
            state->header_visual = argument == "--header-visual";
            state->review_visual = argument == "--review-visual";
            data_directory = fs::absolute(argv[index]);
        }
        else if (argument == "--mcp-bridge") {
            state->mode = HarnessState::Mode::Mcp;
            state->mcp_bridge = true;
        }
        else if (argument == "--live-agent-unavailable")
            state->mode = HarnessState::Mode::LiveAgentUnavailable;
        else if (argument == "--recomputing-capture") {
            if (++index == argc) {
                std::cerr << "--recomputing-capture requires an output directory\n";
                return 2;
            }
            state->mode = HarnessState::Mode::RecomputingCapture;
            state->capture_dir = fs::absolute(argv[index]);
        }
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
        state->mode == HarnessState::Mode::ManualUnconfigured || state->mode == HarnessState::Mode::ManualMcp)
        exit_code = gui_result;
    else if (state->result < 0)
        exit_code = gui_result == 0 ? 1 : gui_result;
    // Static destructors run after this line; a crash without a later
    // ATEXIT line is in a function-local static (the 3mf backup manager,
    // the shell slot, ...), one after it is in a namespace-scope static.
    std::cerr << "HARNESS MAIN RETURNING " << exit_code << '\n';
    return exit_code;
}
