#pragma once

// Help › JusPrin guide…: the app guide (docs/guide) in the Agent page's guide
// mode, packed into the page when it is built (canvas "JusPrin v2", page
// "JusPrin guide window"). The pages at the left with a search box above
// them, the open page at the right. Not modal: a person reads it beside the
// project, so the app keeps working underneath.

#include "slic3r/GUI/JusPrin/Agent/WebViewRelease.hpp"

#include <nlohmann/json_fwd.hpp>

#include <wx/dialog.h>

#include <string>

namespace Slic3r::GUI::JusPrin {
class AgentWebView;
class ShellTheme;
namespace Workspace { class IWorkspace; }
}

namespace Slic3r::GUI::JusPrin {

class GuideWindow : public wxDialog
{
public:
    GuideWindow(wxWindow* parent, const ShellTheme& theme, Workspace::IWorkspace& workspace);
    ~GuideWindow() override;

    void apply_appearance(bool dark);
    // Help › JusPrin guide… again: back in front, even while it is closing.
    void bring_forward();
    // Destroys the window, or hides it and destroys it once no web view is
    // installing its script handler. The title bar's close comes here too.
    void close();

private:
    bool on_page_message(const std::string& type, const nlohmann::json& payload);
    void release();

    const ShellTheme& m_theme;
    WebViewRelease    m_release;
    AgentWebView*     m_view{nullptr};
};

} // namespace Slic3r::GUI::JusPrin
