#include "InvisibleLegacyUiSpike.hpp"

#include "Workspace/Workspace.hpp"
#include "Workspace/WorkspaceProbe.hpp"

#include <boost/log/trivial.hpp>

#include <wx/dialog.h>
#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/timer.h>
#include <wx/toplevel.h>

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>

namespace Slic3r::GUI::JusPrin::Workspace {

namespace {

std::string quoted(const wxString& value)
{
    std::string result = value.ToUTF8().data();
    for (std::size_t offset = 0; (offset = result.find('"', offset)) != std::string::npos; offset += 2)
        result.insert(offset, "\\");
    return '"' + result + '"';
}

std::string rect_text(const wxRect& rect)
{
    return "x=" + std::to_string(rect.x) + " y=" + std::to_string(rect.y) + " width=" + std::to_string(rect.width) +
           " height=" + std::to_string(rect.height);
}

class InvisibleLegacyUiSpikeFrame final : public wxFrame
{
public:
    explicit InvisibleLegacyUiSpikeFrame(wxTopLevelWindow* legacy)
        : wxFrame(nullptr, wxID_ANY, "JusPrin Empty Shell Spike", legacy->GetScreenPosition(), legacy->GetSize(),
                  wxBORDER_NONE | wxFRAME_NO_TASKBAR),
          m_legacy(legacy), m_timer(this)
    {
        if (const char* path = std::getenv("JUSPRIN_INVISIBLE_LEGACY_UI_SPIKE_LOG"))
            m_log_file.open(path, std::ios::out | std::ios::trunc);

        auto* background = new wxPanel(this, wxID_ANY);
        background->SetBackgroundColour(wxColour(241, 243, 245));
        auto* root = new wxBoxSizer(wxVERTICAL);
        root->Add(background, 1, wxEXPAND);
        SetSizer(root);
        SetBackgroundColour(background->GetBackgroundColour());

        Bind(wxEVT_CLOSE_WINDOW, &InvisibleLegacyUiSpikeFrame::on_close, this);
        Bind(wxEVT_CHAR_HOOK, &InvisibleLegacyUiSpikeFrame::on_char_hook, this);
        Bind(wxEVT_TIMER, &InvisibleLegacyUiSpikeFrame::on_timer, this);
        Bind(wxEVT_ACTIVATE, &InvisibleLegacyUiSpikeFrame::on_shell_activate, this);

        m_legacy->Bind(wxEVT_MOVE, &InvisibleLegacyUiSpikeFrame::on_legacy_move, this);
        m_legacy->Bind(wxEVT_SIZE, &InvisibleLegacyUiSpikeFrame::on_legacy_size, this);
        m_legacy->Bind(wxEVT_MAXIMIZE, &InvisibleLegacyUiSpikeFrame::on_legacy_maximize, this);
        m_legacy->Bind(wxEVT_ICONIZE, &InvisibleLegacyUiSpikeFrame::on_legacy_iconize, this);
        m_legacy->Bind(wxEVT_SHOW, &InvisibleLegacyUiSpikeFrame::on_legacy_show, this);
        m_legacy->Bind(wxEVT_ACTIVATE, &InvisibleLegacyUiSpikeFrame::on_legacy_activate, this);
        m_legacy->Bind(wxEVT_DESTROY, &InvisibleLegacyUiSpikeFrame::on_legacy_destroy, this);

        log("created title=" + quoted(GetTitle()) + " legacy_title=" + quoted(m_legacy->GetTitle()));
    }

    ~InvisibleLegacyUiSpikeFrame() override
    {
        stop_tracking();
        log("destroyed");
    }

    void start(IWorkspace& workspace)
    {
        m_workspace = &workspace;
        m_timer.Start(100);
        try_start();
    }

private:
    void try_start()
    {
        if (m_started || m_legacy == nullptr || m_workspace == nullptr || !m_legacy->IsShown())
            return;
        m_started = true;
        sync_coverage("start");
        Show();
        Raise();
        log_state("shown");

        m_probe = show_workspace_probe(this, *m_workspace);
        m_probe->Bind(wxEVT_DESTROY, &InvisibleLegacyUiSpikeFrame::on_probe_destroy, this);
        position_probe();
        m_probe->Raise();
        log("probe_shown title=" + quoted(m_probe->GetLabel()) + " bounds={" + rect_text(m_probe->GetScreenRect()) + '}');
    }

    void log(const std::string& message)
    {
        std::ostringstream ordered;
        ordered << "SHELL seq=" << std::setw(4) << std::setfill('0') << ++m_log_sequence << ' ' << message;
        const std::string line = ordered.str();
        BOOST_LOG_TRIVIAL(info) << line;
        if (m_log_file) {
            m_log_file << line << '\n';
            m_log_file.flush();
        }
    }

    void log_state(const char* event)
    {
        if (m_legacy == nullptr)
            return;
        const wxRect legacy_rect = m_legacy->GetScreenRect();
        const wxRect shell_rect  = GetScreenRect();
        log(std::string(event) + " legacy_shown=" + std::to_string(m_legacy->IsShown()) +
            " legacy_active=" + std::to_string(m_legacy->IsActive()) + " shell_shown=" + std::to_string(IsShown()) +
            " shell_active=" + std::to_string(IsActive()) + " coverage_match=" + std::to_string(legacy_rect == shell_rect) +
            " legacy_bounds={" + rect_text(legacy_rect) + "} shell_bounds={" + rect_text(shell_rect) + '}');
    }

    void sync_coverage(const char* reason)
    {
        if (!m_started || m_legacy == nullptr || m_closing)
            return;
        const wxRect legacy_rect = m_legacy->GetScreenRect();
        if (GetScreenRect() != legacy_rect) {
            SetSize(legacy_rect);
            position_probe();
            log(std::string("coverage_updated reason=") + reason + " bounds={" + rect_text(legacy_rect) + '}');
        }
        if (!m_legacy->IsShown() && !m_legacy_visibility_failure) {
            m_legacy_visibility_failure = true;
            log("failure legacy_not_shown");
        }
    }

    void position_probe()
    {
        if (m_probe == nullptr)
            return;
        const wxRect shell_rect = GetScreenRect();
        const wxSize probe_size = m_probe->GetSize();
        constexpr int margin    = 24;
        const int x_offset      = std::min(margin, std::max(0, shell_rect.width - probe_size.x));
        const int y_offset      = std::min(margin, std::max(0, shell_rect.height - probe_size.y));
        m_probe->Move(shell_rect.GetPosition() + wxPoint(x_offset, y_offset));
    }

    void maintain_z_order()
    {
        if (m_legacy == nullptr || m_closing)
            return;
        if (m_legacy->IsActive()) {
            Raise();
            if (m_probe != nullptr && m_probe->IsShown())
                m_probe->Raise();
            log_state("z_order_restored_after_legacy_activation");
        } else if (IsActive() && m_probe != nullptr && m_probe->IsShown()) {
            m_probe->Raise();
            log_state("probe_restored_after_shell_activation");
        }
    }

    void inspect_top_levels()
    {
        for (wxWindow* window : wxTopLevelWindows) {
            if (window == this || window == m_legacy || window == m_probe || !window->IsShown())
                continue;
            if (!m_reported_top_levels.insert(window).second)
                continue;
            const auto* dialog = dynamic_cast<wxDialog*>(window);
            log("unexpected_top_level_shown title=" + quoted(window->GetLabel()) + " modal=" +
                std::to_string(dialog != nullptr && dialog->IsModal()));
        }
    }

    void stop_tracking()
    {
        if (m_timer.IsRunning())
            m_timer.Stop();
        if (m_legacy == nullptr)
            return;
        m_legacy->Unbind(wxEVT_MOVE, &InvisibleLegacyUiSpikeFrame::on_legacy_move, this);
        m_legacy->Unbind(wxEVT_SIZE, &InvisibleLegacyUiSpikeFrame::on_legacy_size, this);
        m_legacy->Unbind(wxEVT_MAXIMIZE, &InvisibleLegacyUiSpikeFrame::on_legacy_maximize, this);
        m_legacy->Unbind(wxEVT_ICONIZE, &InvisibleLegacyUiSpikeFrame::on_legacy_iconize, this);
        m_legacy->Unbind(wxEVT_SHOW, &InvisibleLegacyUiSpikeFrame::on_legacy_show, this);
        m_legacy->Unbind(wxEVT_ACTIVATE, &InvisibleLegacyUiSpikeFrame::on_legacy_activate, this);
        m_legacy->Unbind(wxEVT_DESTROY, &InvisibleLegacyUiSpikeFrame::on_legacy_destroy, this);
    }

    void on_timer(wxTimerEvent&)
    {
        try_start();
        if (!m_started)
            return;
        sync_coverage("timer");
        maintain_z_order();
        inspect_top_levels();
    }

    void on_close(wxCloseEvent& event)
    {
        if (m_closing) {
            event.Skip();
            return;
        }
        m_closing = true;
        log("close_requested");
        stop_tracking();
        Hide();
        if (m_legacy != nullptr) {
            m_legacy->Show(true);
            m_legacy->Raise();
            log_state("legacy_revealed_after_shell_close");
        }
        event.Skip();
    }

    void on_char_hook(wxKeyEvent& event)
    {
        const int key_code = event.GetKeyCode();
        if (key_code == WXK_ESCAPE || ((event.GetModifiers() & wxMOD_CMD) != 0 && (key_code == 'W' || key_code == 'w'))) {
            log("close_shortcut");
            Close();
            return;
        }
        event.Skip();
    }

    void on_shell_activate(wxActivateEvent& event)
    {
        log(std::string("shell_focus active=") + std::to_string(event.GetActive()));
        if (event.GetActive())
            CallAfter([this] { maintain_z_order(); });
        event.Skip();
    }

    void on_legacy_move(wxMoveEvent& event)
    {
        sync_coverage("legacy_move");
        event.Skip();
    }

    void on_legacy_size(wxSizeEvent& event)
    {
        sync_coverage("legacy_size");
        event.Skip();
    }

    void on_legacy_maximize(wxMaximizeEvent& event)
    {
        CallAfter([this] {
            sync_coverage("legacy_maximize");
            log_state("legacy_maximize");
        });
        event.Skip();
    }

    void on_legacy_iconize(wxIconizeEvent& event)
    {
        log(std::string("legacy_iconize iconized=") + std::to_string(event.IsIconized()));
        event.Skip();
    }

    void on_legacy_show(wxShowEvent& event)
    {
        log(std::string("legacy_visibility shown=") + std::to_string(event.IsShown()));
        CallAfter([this] {
            try_start();
            sync_coverage("legacy_show");
        });
        event.Skip();
    }

    void on_legacy_activate(wxActivateEvent& event)
    {
        log(std::string("legacy_focus active=") + std::to_string(event.GetActive()));
        if (event.GetActive())
            CallAfter([this] { maintain_z_order(); });
        event.Skip();
    }

    void on_legacy_destroy(wxWindowDestroyEvent& event)
    {
        log("legacy_destroyed");
        stop_tracking();
        m_legacy = nullptr;
        if (!IsBeingDeleted())
            Destroy();
        event.Skip();
    }

    void on_probe_destroy(wxWindowDestroyEvent& event)
    {
        if (event.GetEventObject() == m_probe) {
            log("probe_destroyed shell_remains_shown=" + std::to_string(IsShown()));
            m_probe = nullptr;
        }
        event.Skip();
    }

    wxTopLevelWindow* m_legacy{nullptr};
    wxWindow* m_probe{nullptr};
    IWorkspace* m_workspace{nullptr};
    wxTimer m_timer;
    std::ofstream m_log_file;
    std::set<wxWindow*> m_reported_top_levels;
    std::uint64_t m_log_sequence{0};
    bool m_closing{false};
    bool m_started{false};
    bool m_legacy_visibility_failure{false};
};

} // namespace

void show_invisible_legacy_ui_spike(wxWindow* legacy_child, IWorkspace& workspace)
{
    auto* legacy = dynamic_cast<wxTopLevelWindow*>(wxGetTopLevelParent(legacy_child));
    if (legacy == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "Invisible legacy UI spike could not resolve the legacy top-level window";
        return;
    }
    auto* shell = new InvisibleLegacyUiSpikeFrame(legacy);
    shell->start(workspace);
}

} // namespace Slic3r::GUI::JusPrin::Workspace
