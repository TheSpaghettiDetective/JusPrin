// Runs the complete native application and verifies the Phase 1 JusPrin
// production shell: installation, the real Prepare canvas, the Slice and
// Check print flow, resize and project replacement, the failed-install
// fallback, and exact restoration of the stock presentation.
//
// Modes:
//   (default)  automated shell checks, exits with the result
//   --stock    automated stock-mode checks (shell disabled via app config)
//   --manual   installs nothing extra and leaves the app open for a human

#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_Init.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentWebView.hpp"
#include "slic3r/GUI/JusPrin/Shell/AgentPane.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellController.hpp"
#include "slic3r/GUI/JusPrin/Shell/StatusRow.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Notebook.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"

#include <wx/app.h>
#include <wx/glcanvas.h>
#include <wx/timer.h>

#include <boost/filesystem.hpp>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fs = boost::filesystem;

namespace Slic3r::GUI::JusPrin {
namespace {

struct HarnessState
{
    enum class Mode { Shell, Stock, Manual };

    std::atomic<int>  result{-1};
    std::atomic<bool> stop{false};
    std::shared_ptr<void> runner;
    // Phase 4 added real project saves, reopen, and checkpoint exports on
    // top of two full slices; 300 s was regularly exhausted mid-flow.
    std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::now() + std::chrono::seconds(600)};
    Mode mode{Mode::Shell};
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
            load_multi_plate_fixture();
            verify_canvas_interaction();
            begin_slice_check();
        } catch (const std::exception& error) {
            fail(std::string("exception: ") + error.what());
        } catch (...) {
            fail("unknown exception");
        }
    }

private:
    void check(bool condition, const std::string& name)
    {
        std::cerr << "HARNESS CHECK " << name << ' ' << (condition ? "PASS" : "FAIL") << '\n';
        if (!condition)
            ++m_failures;
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
        check(shell->agent_pane() != nullptr && shell->agent_pane()->IsShown(), "agent_pane_shown");
        check(!m_notebook->GetBtnsListCtrl()->IsShown(), "tab_strip_hidden");
        check(!m_plater->is_sidebar_available(), "sidebar_marked_unavailable");
        const GLCanvasPresentationOptions& prepare_options = m_plater->get_view3D_canvas3D()->presentation_options();
        check(!prepare_options.main_toolbar_visible, "prepare_main_toolbar_hidden");
        check(!prepare_options.main_toolbar_input_enabled, "prepare_main_toolbar_input_disabled");
        check(prepare_options.gizmo_picker_visible, "gizmo_picker_still_visible");

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
        verify_agent_bridge();
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
                    "second_conversation_reply_completes", [self] { self->agent_save_reopen(); });
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
                        const auto messages = self->persistence().document().messages(
                            self->persistence().document().active_conversation_id());
                        self->check(!messages.empty(), "saved_messages_survive_reopen");
                        self->check(!self->persistence().document().revisions().empty(),
                                    "saved_revisions_survive_reopen");
                        self->agent_revert_flow();
                    });
            });
    }

    void agent_revert_flow()
    {
        const std::size_t objects_before  = m_plater->model().objects.size();
        const std::string revision_before = persistence().document().current_revision_id();
        check(!revision_before.empty(), "current_revision_known_after_reopen");

        check(m_plater->duplicate_object(0) >= 0, "native_change_for_revert");
        wait_until(
            [this, revision_before] { return persistence().document().current_revision_id() != revision_before; },
            "native_change_captured_revision", [self = shared_from_this(), objects_before, revision_before] {
                self->check(self->m_plater->model().objects.size() == objects_before + 1, "native_change_visible");
                AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
                WebView::RunScript(web_view.webview(),
                                   wxString::FromUTF8("window.__jusprinTest && window.__jusprinTest.revert('" +
                                                      revision_before + "')"));
                self->wait_until(
                    [self, revision_before] {
                        return self->persistence().document().current_revision_id() == revision_before;
                    },
                    "revert_completed_via_page", [self, objects_before, revision_before] {
                        self->check(self->m_plater->model().objects.size() == objects_before,
                                    "revert_restores_native_state");
                        self->check(!self->m_plater->can_redo_project(), "revert_leaves_no_redo");
                        const auto revisions = self->persistence().document().revisions();
                        self->check(!revisions.empty() && revisions.back().id == revision_before,
                                    "revert_truncates_timeline");
                        self->agent_import_and_clean_share();
                    });
            });
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
        check(m_plater->get_view3D_canvas3D()->presentation_options().main_toolbar_visible,
              "prepare_main_toolbar_restored");
        m_frame->Layout();
        check(m_plater->canvas3D()->get_wxglcanvas()->GetSize().GetWidth() > 200, "stock_canvas_usable_after_restore");
    }

    // Polls through a one-shot wxTimer rather than a self-reposting
    // CallAfter: a pending-event spin keeps wxApp's pending queue non-empty,
    // which starves any nested YieldFor on the stack (wx's WKWebView
    // AddScriptMessageHandler runs script through one) and deadlocks the
    // WebView setup this harness is waiting on.
    void wait_until(std::function<bool()> condition, std::string name, std::function<void()> then)
    {
        m_wait_condition = std::move(condition);
        m_wait_name      = std::move(name);
        m_wait_then      = std::move(then);
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
            then();
            return;
        }
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
    std::size_t                   m_objects_before_tool{0};
    std::string                   m_saved_project_id;
    std::string                   m_saved_project_file;
    std::size_t                   m_saved_project_bytes{0};
    double                        m_save_ms{0.0};

    wxEvtHandler          m_poll_handler;
    wxTimer               m_poll_timer{&m_poll_handler};
    std::function<bool()> m_wait_condition;
    std::string           m_wait_name;
    std::function<void()> m_wait_then;
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
        if (state->mode == HarnessState::Mode::Manual)
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
    using namespace Slic3r;
    using namespace Slic3r::GUI;
    using namespace Slic3r::GUI::JusPrin;

    const fs::path original_directory = fs::current_path();
    const fs::path data_directory = fs::temp_directory_path() / fs::unique_path("jusprin-shell-%%%%-%%%%-%%%%");
    fs::create_directories(data_directory / "log");

    auto state = std::make_shared<HarnessState>();
    std::vector<char*> gui_arguments;
    gui_arguments.reserve(static_cast<std::size_t>(argc));
    gui_arguments.emplace_back(argv[0]);
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--stock")
            state->mode = HarnessState::Mode::Stock;
        else if (argument == "--manual")
            state->mode = HarnessState::Mode::Manual;
        else
            gui_arguments.emplace_back(argv[index]);
    }

    {
        std::ifstream base(std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/jusprin/OrcaSlicer.conf");
        std::string   config((std::istreambuf_iterator<char>(base)), std::istreambuf_iterator<char>());
        if (state->mode == HarnessState::Mode::Stock) {
            const std::string anchor = "\"language\": \"en_US\",";
            config.replace(config.find(anchor), anchor.size(), anchor + "\n    \"jusprin_shell\": \"0\",");
        }
        std::ofstream out((data_directory / "OrcaSlicer.conf").string());
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

    fs::current_path(original_directory);
    if (state->result == 0)
        fs::remove_all(data_directory);
    else
        std::cerr << "HARNESS DATA DIR kept for inspection: " << data_directory.string() << '\n';
    if (state->mode == HarnessState::Mode::Manual)
        return gui_result;
    if (state->result < 0)
        return gui_result == 0 ? 1 : gui_result;
    return state->result;
}
