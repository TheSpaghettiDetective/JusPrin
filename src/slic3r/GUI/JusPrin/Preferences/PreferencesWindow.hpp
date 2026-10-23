#pragma once

// JusPrin's Preferences window: the Agent page in its Preferences mode, in a
// window of its own over whatever screen is open. The page draws every
// section, with the same Agent connection rows setup draws; this side reads
// and writes the settings (PreferencesSettings) and carries out what a
// change needs through OrcaSlicer's own functions.
//
// Modal, so nothing in the project can change under it. Two things happen
// only once it is gone, because each replaces windows it sits on: "Restart
// now" after a new language quits and starts JusPrin again, and the
// "OrcaSlicer preferences…" link opens OrcaSlicer's own dialog.

#include "slic3r/GUI/JusPrin/Agent/AgentHost.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"

#include <nlohmann/json_fwd.hpp>

#include <wx/dialog.h>
#include <wx/timer.h>

#include <memory>
#include <string>

namespace Slic3r::GUI::JusPrin {
class AgentWebView;
namespace Workspace { class IWorkspace; }
namespace Agent { class ProjectPersistence; }
}

namespace Slic3r::GUI::JusPrin::Preferences {

struct Outcome;

// What the window was opened for. Empty opens it on Account.
struct OpenRequest
{
    // account, agent, appearance, view3d, projects or updates.
    std::string section;
};

// What to do once the window is gone.
struct AfterClose
{
    enum class Kind { Nothing, Restart, OrcaPreferences };
    Kind kind{Kind::Nothing};
};

class PreferencesWindow : public wxDialog
{
public:
    // `docked` is the docked Agent panel's host, whose AI tool connections
    // this window offers too.
    PreferencesWindow(wxWindow*              parent,
                      const ShellTheme&      theme,
                      Workspace::IWorkspace& workspace,
                      const Agent::AgentHost& docked,
                      OpenRequest            request);
    ~PreferencesWindow() override;

    const AfterClose& after_close() const { return m_after; }

private:
    bool           on_page_message(const std::string& type, const nlohmann::json& payload);
    nlohmann::json state() const;
    void           push_state();
    void           carry_out(const Outcome& outcome);
    void           close_for(AfterClose after);
    void           apply_appearance();
    void           on_pump(wxTimerEvent& event);

    const ShellTheme&                          m_theme;
    OpenRequest                                m_request;
    std::unique_ptr<Agent::ProjectPersistence> m_persistence;
    AgentWebView*                              m_view{nullptr};
    wxTimer                                    m_pump;
    AfterClose                                 m_after;
    int                                        m_update_listener{0};
};

// Whether open_preferences() should open this window for these arguments.
// OrcaSlicer asks for a tab or an option only to point at its own dialog,
// and the window's own link asks for that dialog.
bool answers_open_request(size_t open_on_tab, const std::string& highlight_option);

// Carries out what the window was closed for, after it is gone.
void run_after_close(const AfterClose& after);

// Saves the project, closes the main window (OrcaSlicer still asks about
// anything unsaved; cancelling there cancels the restart), and starts JusPrin
// again with the same command line as this process exits.
void restart_application();

} // namespace Slic3r::GUI::JusPrin::Preferences
