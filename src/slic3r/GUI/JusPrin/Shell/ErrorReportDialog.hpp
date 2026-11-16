#pragma once

// The dialog an unexpected error opens (canvas page "Agent UI · States &
// Components", boards agent-ui-11 to agent-ui-15): "Something went wrong on
// our side", what a report holds, a box to report such errors automatically
// from now on, Cancel and "Report to JusPrin team". It says nothing more
// specific about the failure, and nothing after the button: no "report sent".
// Only report_unexpected_error (Support/ErrorReport.hpp) opens it.

#include "slic3r/GUI/JusPrin/Support/ErrorReport.hpp"

#include <wx/dialog.h>

class Button;
class wxPanel;
class wxStaticBitmap;
class wxStaticText;

namespace Slic3r::GUI::JusPrin {

class CheckToggle;
class ShellTheme;

class ErrorReportDialog final : public wxDialog
{
public:
    struct Answer
    {
        bool report{false};        // "Report to JusPrin team" was pressed
        bool automatically{false}; // ... with the box ticked
    };

    // Shows the dialog over the window in front and waits for the answer.
    // Cancel or closing the window answers nothing: nothing is sent or saved.
    static Answer ask(const ErrorReport& report);

    // The open dialog, or nullptr; for the shell harness.
    static ErrorReportDialog* current();
    void       toggle_contents();
    CheckToggle* automatic_box() const { return m_automatic; }
    Button*      report_button() const { return m_report; }
    Button*      cancel_button() const { return m_cancel; }

private:
    ErrorReportDialog(wxWindow* parent, const ShellTheme& theme, const ErrorReport& report);
    ~ErrorReportDialog() override;

    wxPanel* build_contents(const ErrorReport& report);
    void     update_report_label();

    const ShellTheme& m_theme;
    wxStaticBitmap*   m_chevron{nullptr};
    wxPanel*          m_contents{nullptr};
    CheckToggle*      m_automatic{nullptr};
    Button*           m_cancel{nullptr};
    Button*           m_report{nullptr};
};

} // namespace Slic3r::GUI::JusPrin
