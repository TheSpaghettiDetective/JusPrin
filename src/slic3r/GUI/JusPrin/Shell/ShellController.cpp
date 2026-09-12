#include "ShellController.hpp"

#include "AgentPane.hpp"
#include "StatusRow.hpp"

#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentConfiguration.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentWebView.hpp"
#include "slic3r/GUI/JusPrin/Brand/BrandPalette.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Notebook.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <wx/dcbuffer.h>
#include <wx/panel.h>
#include <wx/sizer.h>

#include <algorithm>
#include <functional>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin {

namespace {

class AgentPaneResizeHandle final : public wxPanel
{
public:
    // What the divider needs from the shell: the pane's width now, a new
    // width while dragging, and the double click that closes the pane.
    struct Callbacks
    {
        std::function<int()>     width;
        std::function<void(int)> set_width;
        std::function<void()>    toggle;
    };

    AgentPaneResizeHandle(wxWindow* parent, const ShellTheme& theme, Callbacks callbacks)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition,
                  parent->FromDIP(wxSize(theme.metrics().agent_pane.resize_handle_width, -1)),
                  wxBORDER_NONE)
        , m_theme(theme)
        , m_callbacks(std::move(callbacks))
    {
        SetName(_L("Resize Agent panel"));
        SetToolTip(_L("Drag to resize the Agent panel"));
        SetMinSize(parent->FromDIP(wxSize(m_theme.metrics().agent_pane.resize_handle_width, -1)));
        SetCursor(wxCursor(wxCURSOR_SIZEWE));
        SetBackgroundStyle(wxBG_STYLE_PAINT);

        Bind(wxEVT_PAINT, &AgentPaneResizeHandle::on_paint, this);
        Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { m_hovered = true; Refresh(); });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { m_hovered = false; Refresh(); });
        Bind(wxEVT_LEFT_DOWN, &AgentPaneResizeHandle::on_left_down, this);
        Bind(wxEVT_LEFT_UP, &AgentPaneResizeHandle::on_left_up, this);
        Bind(wxEVT_LEFT_DCLICK, &AgentPaneResizeHandle::on_double_click, this);
        Bind(wxEVT_MOTION, &AgentPaneResizeHandle::on_motion, this);
        Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { m_dragging = false; Refresh(); });
    }

private:
    void on_paint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        const ShellPalette& palette = m_theme.palette(wxGetApp().dark_mode());
        dc.SetBackground(wxBrush(palette.surface_subtle));
        dc.Clear();

        const wxColour divider = m_dragging ? palette.action_primary :
                                 m_hovered  ? palette.border_strong : palette.border_subtle;
        const int line_width = FromDIP(m_theme.metrics().agent_pane.resize_handle_line_width);
        dc.SetPen(wxPen(divider, line_width));
        const int center = GetClientSize().x / 2;
        dc.DrawLine(center, 0, center, GetClientSize().y);
    }

    void on_left_down(wxMouseEvent& event)
    {
        m_drag_origin_x = ClientToScreen(event.GetPosition()).x;
        m_drag_origin_width = m_callbacks.width();
        m_dragging = true;
        CaptureMouse();
        Refresh();
    }

    void on_left_up(wxMouseEvent&)
    {
        end_drag();
    }

    void on_double_click(wxMouseEvent&)
    {
        // wx sends down/up before the double click, so a drag may be open.
        end_drag();
        m_callbacks.toggle();
    }

    void on_motion(wxMouseEvent& event)
    {
        if (!m_dragging || !HasCapture())
            return;
        const int pointer_x = ClientToScreen(event.GetPosition()).x;
        // A drag only ever sizes the pane; it stops at the minimum width and
        // never closes it. Closing is deliberate -- the header button or a
        // double click on this divider -- because a drag that closed the pane
        // on its own would take the conversation off the screen as a side
        // effect of aiming for a narrow one.
        m_callbacks.set_width(m_drag_origin_width + m_drag_origin_x - pointer_x);
    }

    void end_drag()
    {
        if (!m_dragging)
            return;
        m_dragging = false;
        if (HasCapture())
            ReleaseMouse();
        Refresh();
    }

    const ShellTheme& m_theme;
    Callbacks m_callbacks;
    bool m_hovered{false};
    bool m_dragging{false};
    int m_drag_origin_x{0};
    int m_drag_origin_width{0};
};

std::unique_ptr<ShellController>& shell_slot()
{
    static std::unique_ptr<ShellController> shell;
    return shell;
}

} // namespace

ShellController::ShellController()
{
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        if (!m_agent_pane) return;
        auto& host = m_agent_pane->web_view().host();
        host.pump_stream();
        host.pump_tools();
        host.pump_setup();
    }, m_runtime_timer.GetId());
}

ShellController::~ShellController()
{
    m_runtime_timer.Stop();
    if (m_installed)
        uninstall();
}

void ShellController::install(MainFrame& frame, Notebook& tabpanel, wxSizer& main_sizer)
{
    if (m_installed)
        throw std::runtime_error("the JusPrin shell is already installed");

    Plater* plater = frame.plater();
    if (plater == nullptr)
        throw std::runtime_error("the Plater is not constructed");
    if (tabpanel.FindPage(plater) != MainFrame::tp3DEditor)
        throw std::runtime_error("the Prepare page is not where the shell expects it");
    if (main_sizer.GetItem(&tabpanel) == nullptr)
        throw std::runtime_error("the tab panel is not in the main layout");

    // The design tokens were loaded once, at startup, by the brand palette.
    // Checked before touching any layout so a broken resource bundle leaves
    // the stock presentation untouched.
    m_theme = brand_theme();
    if (m_theme == nullptr)
        throw std::runtime_error("the JusPrin design tokens did not load; the brand palette logged why");

    m_frame     = &frame;
    m_tabpanel  = &tabpanel;
    m_main_sizer = &main_sizer;
    m_plater    = plater;

    // Saved before any mutation so a partial-install rollback restores the
    // real prior state, not a default.
    m_saved_collapse_toolbar_enabled = plater->get_collapse_toolbar().is_enabled();
    m_saved_auto_preview_after_slice = plater->auto_preview_after_slice();

    try {
        m_workspace = std::make_unique<Workspace::OrcaWorkspaceAdapter>(*plater);

        // Conversation state, stored inside the project's
        // auxiliary directory and mirrored to a per-project local recovery
        // store under the application data dir.
        Agent::ProjectPersistence::Config persistence_config;
        persistence_config.recovery_root = (boost::filesystem::path(data_dir()) / "jusprin" / "recovery").string();
        m_persistence = std::make_unique<Agent::ProjectPersistence>(*m_workspace, std::move(persistence_config));

        Agent::AgentRuntime agent = Agent::load_agent_runtime(wxGetApp().app_config);

        m_status_row = new StatusRow(&frame, *m_theme, *plater, tabpanel, *m_persistence);
        m_agent_pane = new AgentPane(&frame, *m_theme, *m_workspace, *m_persistence, agent.availability,
                                     std::move(agent.service), std::move(agent.setup),
                                     (boost::filesystem::path(data_dir()) / "jusprin" / "mcp.json").string());
        m_agent_pane_preferred_width = frame.FromDIP(m_theme->metrics().agent_pane.min_width);
        m_agent_resize_handle = new AgentPaneResizeHandle(
            &frame, *m_theme,
            AgentPaneResizeHandle::Callbacks{
                [this] { return m_agent_pane == nullptr ? 0 : m_agent_pane->GetSize().x; },
                [this](int width) { request_agent_pane_width(width); },
                [this] { toggle_agent_pane(); }});
        m_status_row->set_agent_pane_toggle([this] { toggle_agent_pane(); });
        m_status_row->set_agent_pane_collapsed(m_agent_pane_collapsed);

        // The header posts its after-swap line into the thread. Wiring it here
        // rather than giving StatusRow the AgentHost keeps the header free of
        // any Agent dependency, and the weak reference means a torn-down pane
        // simply stops accepting notes.
        m_status_row->set_note_sink([pane = wxWeakRef<AgentPane>(m_agent_pane)](const wxString& text) {
            if (pane) pane->web_view().host().post_note(text.ToUTF8().data());
        });

        // Adopt the currently open project once the host has registered its
        // listeners, so the initial document reaches the pane too.
        m_persistence->attach();

        // One shell-owned pump continues throughout WebView page reloads.
        // The page handshake gates delivery, not native MCP execution.
        m_runtime_timer.Start(33);

        main_sizer.Detach(&tabpanel);
        m_center_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_center_sizer->Add(&tabpanel, 1, wxEXPAND);
        m_center_sizer->Add(m_agent_resize_handle, 0, wxEXPAND);
        m_center_sizer->Add(m_agent_pane, 0, wxEXPAND);
        main_sizer.Insert(0, m_status_row, 0, wxEXPAND);
        main_sizer.Add(m_center_sizer, 1, wxEXPAND);

        tabpanel.GetBtnsListCtrl()->Hide();
        plater->set_sidebar_available(false);
        plater->set_auto_preview_after_slice(false);

        // The collapse toolbar belongs to the Plater and is shared by the
        // Prepare and Preview canvases, so this controller is its single
        // owner for the shell's lifetime.
        plater->get_collapse_toolbar().set_enabled(false);

        // Hide the whole legacy canvas-overlay layer on the Prepare canvas
        // (main toolbar, gizmo picker, plate controls, navigator, canvas menu)
        // and the per-plate corner icons; the fork's own UI owns the canvas.
        // The active gizmo stays interactive so shell controls can drive it.
        // Add-model remains available through File > Import and drag-drop.
        if (GLCanvas3D* prepare_canvas = plater->get_view3D_canvas3D()) {
            m_prepare_canvas_presentation.attach(*prepare_canvas);
        }

        // The frame may outlive a runtime detach, so use a tracked event sink
        // and disconnect it when uninstalling the controller.
        frame.Bind(wxEVT_DESTROY, &ShellController::on_frame_destroy, this);
        frame.Bind(wxEVT_SIZE, &ShellController::on_frame_size, this);

        m_installed = true;
        apply_current_appearance();
        m_status_row->refresh();
        frame.Layout();
        apply_agent_pane_width();
    } catch (...) {
        m_installed = true; // let uninstall() undo whatever was applied
        uninstall();
        throw;
    }
}

void ShellController::on_frame_destroy(wxWindowDestroyEvent& event)
{
    if (event.GetWindow() == m_frame) {
        m_runtime_timer.Stop();
        m_installed = false;
        m_prepare_canvas_presentation.abandon();
        // This controller lives in a static slot and outlives every window,
        // so whatever it owns that unbinds from the Plater has to go now,
        // while the frame's children still exist: wx sends this event from
        // the top-level window's destructor, before DestroyChildren. Left in
        // place, the slot's destructor ran ~OrcaWorkspaceAdapter against a
        // deleted Plater after main had returned -- an access violation at
        // process exit in every run that did not detach the shell by hand.
        // Same order as uninstall(): the pane and status row first, since
        // their destructors talk to persistence and the workspace, then
        // those two. The remaining children die with the frame.
        if (m_agent_pane != nullptr) {
            m_agent_pane->Destroy();
            m_agent_pane = nullptr;
        }
        if (m_status_row != nullptr) {
            m_status_row->Destroy();
            m_status_row = nullptr;
        }
        m_persistence.reset();
        m_workspace.reset();
    }
    event.Skip();
}

void ShellController::on_frame_size(wxSizeEvent& event)
{
    event.Skip();
    if (m_installed)
        apply_agent_pane_width();
}

int ShellController::agent_pane_width_within(int width) const
{
    if (m_theme == nullptr || m_frame == nullptr)
        return width;
    const AgentPaneMetrics& metrics = m_theme->metrics().agent_pane;
    const int min_width = m_frame->FromDIP(metrics.min_width);
    const int workspace_min_width = m_frame->FromDIP(metrics.workspace_min_width);
    const int handle_width = m_frame->FromDIP(metrics.resize_handle_width);
    const int max_width = std::max(min_width, m_frame->GetClientSize().x - workspace_min_width - handle_width);
    return std::clamp(width, min_width, max_width);
}

void ShellController::request_agent_pane_width(int width)
{
    // Remember what the pane can actually take, not what the pointer asked
    // for. A drag that runs past the edge of a small window would otherwise
    // record a width the pane never had, and the pane would claim it -- at the
    // workspace's expense -- the next time the window grew enough to allow it.
    // A width the window merely cannot afford right now is a different case:
    // apply_agent_pane_width() narrows the pane without touching the
    // preference, so growing the window restores the width the person chose.
    m_agent_pane_preferred_width = agent_pane_width_within(width);
    apply_agent_pane_width();
}

void ShellController::apply_agent_pane_width()
{
    if (m_theme == nullptr || m_frame == nullptr || m_center_sizer == nullptr || m_agent_pane == nullptr)
        return;
    if (m_agent_pane_collapsed)
        return;
    const int width = agent_pane_width_within(m_agent_pane_preferred_width);
    m_center_sizer->SetItemMinSize(m_agent_pane, width, -1);
    // Lay out from the frame, not from m_center_sizer. A sizer lays itself out
    // against the dimension it was last given, which during a frame resize is
    // still the previous client width; the frame reads the current one from the
    // window. Laying out the sizer directly leaves the workspace and the
    // divider sized for the old width while the pane takes the new one, so they
    // overlap, and the next resize swaps which of them is stale.
    m_frame->Layout();
}

void ShellController::set_agent_pane_collapsed(bool collapsed)
{
    if (m_agent_pane == nullptr || m_agent_resize_handle == nullptr || m_agent_pane_collapsed == collapsed)
        return;
    m_agent_pane_collapsed = collapsed;
    // Hidden, never destroyed: the conversation, the loaded page, and the MCP
    // runtime all outlive a collapse. A hidden sizer item takes no space, so
    // the workspace grows into the whole width with no second width policy.
    m_agent_pane->Show(!collapsed);
    m_agent_resize_handle->Show(!collapsed);
    if (m_status_row != nullptr)
        m_status_row->set_agent_pane_collapsed(collapsed);
    // Expanding re-applies the width policy, which lays out on its way; a
    // collapsed pane has no width to apply, so it lays out here.
    if (collapsed)
        m_frame->Layout();
    else
        apply_agent_pane_width();
}

void ShellController::uninstall()
{
    m_runtime_timer.Stop();
    if (!m_installed)
        return;
    m_installed = false;
    m_agent_pane_collapsed = false;
    m_frame->Unbind(wxEVT_DESTROY, &ShellController::on_frame_destroy, this);
    m_frame->Unbind(wxEVT_SIZE, &ShellController::on_frame_size, this);

    m_prepare_canvas_presentation.detach();
    m_plater->get_collapse_toolbar().set_enabled(m_saved_collapse_toolbar_enabled);
    m_plater->set_sidebar_available(true);
    m_plater->set_auto_preview_after_slice(m_saved_auto_preview_after_slice);
    m_tabpanel->GetBtnsListCtrl()->Show();

    if (m_center_sizer != nullptr) {
        m_center_sizer->Detach(m_tabpanel);
        if (m_agent_resize_handle != nullptr)
            m_center_sizer->Detach(m_agent_resize_handle);
        if (m_agent_pane != nullptr)
            m_center_sizer->Detach(m_agent_pane);
        m_main_sizer->Detach(m_center_sizer);
        delete m_center_sizer;
        m_center_sizer = nullptr;
    }
    if (m_status_row != nullptr)
        m_main_sizer->Detach(m_status_row);
    if (m_main_sizer->GetItem(m_tabpanel) == nullptr)
        m_main_sizer->Add(m_tabpanel, 1, wxEXPAND | wxTOP, 0);

    if (m_status_row != nullptr) {
        m_status_row->Destroy();
        m_status_row = nullptr;
    }
    if (m_agent_resize_handle != nullptr) {
        m_agent_resize_handle->Destroy();
        m_agent_resize_handle = nullptr;
    }
    if (m_agent_pane != nullptr) {
        m_agent_pane->Destroy();
        m_agent_pane = nullptr;
    }
    // After the pane (and with it the Agent host) is gone.
    m_persistence.reset();
    m_workspace.reset();

    m_frame->Layout();
}

void ShellController::apply_current_appearance()
{
    const bool dark = wxGetApp().dark_mode();
    if (m_status_row != nullptr)
        m_status_row->apply_appearance(dark);
    if (m_agent_pane != nullptr)
        m_agent_pane->apply_appearance(dark);
    if (m_agent_resize_handle != nullptr)
        m_agent_resize_handle->Refresh();
}

void attach_shell(MainFrame& frame, Notebook* tabpanel, wxSizer* main_sizer)
{
    if (tabpanel == nullptr || main_sizer == nullptr)
        return;
    if (!wxGetApp().is_editor())
        return;
    if (wxGetApp().app_config != nullptr && wxGetApp().app_config->get("jusprin_shell") == "0")
        return;

    auto controller = std::make_unique<ShellController>();
    try {
        controller->install(frame, *tabpanel, *main_sizer);
    } catch (const std::exception& error) {
        BOOST_LOG_TRIVIAL(error) << "JusPrin shell could not be installed, keeping the standard "
                                    "presentation: " << error.what();
        return;
    }
    shell_slot() = std::move(controller);
}

ShellController* installed_shell()
{
    return shell_slot().get();
}

void detach_shell()
{
    if (shell_slot() != nullptr) {
        shell_slot()->uninstall();
        shell_slot().reset();
    }
}

} // namespace Slic3r::GUI::JusPrin
