#include "ShellController.hpp"

#include "AgentPane.hpp"
#include "StatusRow.hpp"

#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentConfiguration.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Notebook.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <wx/sizer.h>

#include <stdexcept>

namespace Slic3r::GUI::JusPrin {

namespace {

std::unique_ptr<ShellController>& shell_slot()
{
    static std::unique_ptr<ShellController> shell;
    return shell;
}

} // namespace

ShellController::~ShellController()
{
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

    // Load the packaged design tokens before touching any layout so a broken
    // resource bundle leaves the stock presentation untouched.
    m_theme = ShellTheme::load_from_resources();

    m_frame     = &frame;
    m_tabpanel  = &tabpanel;
    m_main_sizer = &main_sizer;
    m_plater    = plater;

    // Saved before any mutation so a partial-install rollback restores the
    // real prior state, not a default.
    m_saved_collapse_toolbar_enabled = plater->get_collapse_toolbar().is_enabled();

    try {
        m_workspace = std::make_unique<Workspace::OrcaWorkspaceAdapter>(*plater);

        // Conversation and revision state, stored inside the project's
        // auxiliary directory and mirrored to a per-project local recovery
        // store under the application data dir.
        Agent::ProjectPersistence::Config persistence_config;
        persistence_config.recovery_root = (boost::filesystem::path(data_dir()) / "jusprin" / "recovery").string();
        m_persistence = std::make_unique<Agent::ProjectPersistence>(*m_workspace, std::move(persistence_config));

        Agent::AgentRuntime agent = Agent::load_agent_runtime(wxGetApp().app_config);

        m_status_row = new StatusRow(&frame, m_theme, *plater, tabpanel, *m_persistence);
        m_agent_pane = new AgentPane(&frame, m_theme, *m_workspace, *m_persistence, agent.availability,
                                     std::move(agent.service), std::move(agent.setup));

        // Adopt the currently open project once the host has registered its
        // listeners, so the initial document reaches the pane too.
        m_persistence->attach();

        main_sizer.Detach(&tabpanel);
        m_center_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_center_sizer->Add(&tabpanel, 1, wxEXPAND);
        m_center_sizer->Add(m_agent_pane, 0, wxEXPAND);
        main_sizer.Insert(0, m_status_row, 0, wxEXPAND);
        main_sizer.Add(m_center_sizer, 1, wxEXPAND);

        tabpanel.GetBtnsListCtrl()->Hide();
        plater->set_sidebar_available(false);

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

        // The controller outlives the frame (it lives in a static slot), so
        // stop touching widgets once the frame starts tearing them down.
        frame.Bind(wxEVT_DESTROY, [this](wxWindowDestroyEvent& event) {
            if (event.GetWindow() == m_frame) {
                m_installed = false;
                m_prepare_canvas_presentation.abandon();
            }
            event.Skip();
        });

        m_installed = true;
        apply_current_appearance();
        m_status_row->refresh();
        frame.Layout();
    } catch (...) {
        m_installed = true; // let uninstall() undo whatever was applied
        uninstall();
        throw;
    }
}

void ShellController::uninstall()
{
    if (!m_installed)
        return;
    m_installed = false;

    m_prepare_canvas_presentation.detach();
    m_plater->get_collapse_toolbar().set_enabled(m_saved_collapse_toolbar_enabled);
    m_plater->set_sidebar_available(true);
    m_tabpanel->GetBtnsListCtrl()->Show();

    if (m_center_sizer != nullptr) {
        m_center_sizer->Detach(m_tabpanel);
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
