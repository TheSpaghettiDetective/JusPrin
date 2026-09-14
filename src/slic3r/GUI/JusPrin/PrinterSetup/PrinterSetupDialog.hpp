#pragma once

#include "PrinterDiscovery.hpp"
#include "PrinterSetupController.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"

#include <wx/timer.h>

#include <memory>
#include <functional>
#include <string>
#include <vector>

class Button;
class wxBoxSizer;
class wxHyperlinkCtrl;
class wxPanel;
class wxTextCtrl;
class wxWindow;
class wxStaticText;

namespace Slic3r::GUI::JusPrin {
class AgentWebView;
}

namespace Slic3r::GUI::JusPrin::PrinterSetup {

// Hands a network printer its LAN access code. Returns false with a message
// the dialog shows when the code is rejected or the printer is gone.
using ConnectFn = std::function<bool(const DiscoveredPrinter&, const std::string& access_code, wxString& error)>;

// Builds a throwaway, setup-only AgentWebView parented to the dialog (no MCP
// wiring, its own independent AgentService/AgentSetupService). Called at most
// once per dialog, lazily, the first time "Set up the agent" is clicked.
using MakeSetupWebViewFn = std::function<std::unique_ptr<Slic3r::GUI::JusPrin::AgentWebView>(wxWindow*)>;

class PrinterSetupDialog final : public DPIDialog
{
public:
    PrinterSetupDialog(wxWindow* parent, const ShellTheme& theme, bool dark,
                       std::unique_ptr<PrinterSetupController> controller,
                       std::vector<DiscoveredPrinter> discovered,
                       bool agent_connected,
                       MakeSetupWebViewFn make_setup_webview,
                       ConnectFn connect,
                       std::function<void()> on_agent_configured = {});
    ~PrinterSetupDialog() override;

protected:
    void on_dpi_changed(const wxRect&) override;

private:
    void rebuild();
    void rebuild_later();
    void build_header(wxBoxSizer& content);
    void build_initial(wxBoxSizer& content);
    void build_no_agent(wxBoxSizer& content);
    void build_network_section(wxBoxSizer& content);
    void build_recognizing(wxBoxSizer& content);
    void build_recognized(wxBoxSizer& content);
    void build_ambiguous(wxBoxSizer& content);
    void build_error(wxBoxSizer& content);
    wxPanel* candidate_card(wxWindow* parent, const PrinterCandidate& candidate, bool compact,
                            const wxString& action, std::function<void()> invoke);
    wxStaticText* label(wxWindow* parent, const wxString& text, TextRole role, const wxColour& color);
    Button* button(wxWindow* parent, const wxString& text, bool primary, std::function<void()> invoke);
    // An accent link is the violet, underlined "go elsewhere" link of 18a and 18f.
    wxHyperlinkCtrl* link(wxWindow* parent, const wxString& text, std::function<void()> invoke, bool accent = false);
    std::string description_value() const;
    void choose_photo(bool submit_after_load = false);
    bool load_photo(const wxString& path);
    void submit();
    // Clears every piece of evidence, including a staged photo, and returns to 18a.
    void start_over();
    void open_manual_setup();
    void set_up_agent();
    void on_timer(wxTimerEvent&);

    const ShellTheme& m_theme;
    const ShellPalette& m_palette;
    std::unique_ptr<PrinterSetupController> m_controller;
    std::vector<DiscoveredPrinter> m_discovered;
    bool m_agent_connected{false};
    MakeSetupWebViewFn m_make_setup_webview;
    // Lazily built the first time "Set up the agent" is clicked; destroyed
    // once its setup flow completes. Parented directly to the dialog (not to
    // m_root's content sizer) so an unrelated rebuild() -- e.g. a DPI change
    // while it is showing -- cannot destroy it along with the rest.
    std::unique_ptr<Slic3r::GUI::JusPrin::AgentWebView> m_setup_webview;
    bool m_showing_agent_setup{false};
    wxTimer m_setup_pump_timer{this};
    // Notifies the shell that the docked Agent panel's own availability may
    // now be stale, once this dialog's embedded setup flow succeeds.
    std::function<void()> m_on_agent_configured;
    ConnectFn m_connect;
    wxBoxSizer* m_root{nullptr};
    wxTextCtrl* m_description{nullptr};
    bool m_description_is_prompt{false};
    wxTimer m_timer;
    PrinterEvidence m_draft;
    wxString m_photo_error;
    // Kept across the rebuild that shows a rejected code.
    std::string m_access_code;
    wxString m_access_error;
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
