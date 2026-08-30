#pragma once

#include "ShellTheme.hpp"

#include <wx/panel.h>

class wxStaticText;

namespace Slic3r::GUI::JusPrin {

// Fixed right-hand region reserved for the Agent conversation. Phase 1 ships
// no Agent, so this pane shows an honest unavailable state instead of a mock
// conversation UI. Later phases replace the body with the Agent WebView.
class AgentPane : public wxPanel
{
public:
    AgentPane(wxWindow* parent, const ShellTheme& theme);

    void apply_appearance(bool dark);

private:
    const ShellTheme& m_theme;
    wxStaticText*     m_title{nullptr};
    wxStaticText*     m_message{nullptr};
    wxStaticText*     m_detail{nullptr};
};

} // namespace Slic3r::GUI::JusPrin
