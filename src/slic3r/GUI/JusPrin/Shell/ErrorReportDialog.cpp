#include "ErrorReportDialog.hpp"

#include "CheckPrintReportPanel.hpp"
#include "LucideIcon.hpp"
#include "ShellRecipes.hpp"
#include "ShellTheme.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/JusPrin/Brand/BrandPalette.hpp"

#include <boost/log/trivial.hpp>

#include <wx/dcclient.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/toplevel.h>

#include <algorithm>

namespace Slic3r::GUI::JusPrin {

namespace {

ErrorReportDialog* g_open = nullptr;

// The dialog's width (the boards' max-width) and the column the row names
// take in "See what the report contains".
constexpr int kWidth     = 500;
constexpr int kNameWidth = 76;

// Breaks `text` into lines no wider than `width` in `font`, at spaces where it
// can and inside a word where one is wider than the line, as the boards'
// overflow-wrap: anywhere does: an error names paths with no space to break at.
wxString wrap_anywhere(const wxString& text, const wxFont& font, int width, wxWindow& window)
{
    wxClientDC dc(&window);
    dc.SetFont(font);
    wxString out;
    for (const wxString& paragraph : wxSplit(text, '\n', '\0')) {
        wxString line;
        const auto fits = [&](const wxString& candidate) { return dc.GetTextExtent(candidate).x <= width; };
        for (const wxString& word : wxSplit(paragraph, ' ', '\0')) {
            const wxString joined = line.empty() ? word : line + " " + word;
            if (fits(joined)) {
                line = joined;
                continue;
            }
            if (!line.empty()) {
                out += line + "\n";
                line.clear();
            }
            for (const wxUniChar c : word) {
                if (!line.empty() && !fits(line + c)) {
                    out += line + "\n";
                    line.clear();
                }
                line += c;
            }
        }
        out += line + "\n";
    }
    out.RemoveLast();
    return out;
}

wxWindow* window_in_front()
{
    if (wxWindow* active = wxGetActiveWindow())
        if (wxWindow* top = wxGetTopLevelParent(active); top != nullptr && top->IsShown())
            return top;
    return wxGetApp().mainframe;
}

} // namespace

ErrorReportDialog::Answer ErrorReportDialog::ask(const ErrorReport& report)
{
    const ShellTheme* theme = brand_theme();
    if (theme == nullptr) {
        // Without the design tokens the app keeps OrcaSlicer's own look and
        // this dialog has nothing to be drawn with. Not built: an undrawn
        // dialog cannot ask, so nothing is sent.
        BOOST_LOG_TRIVIAL(error) << "JusPrin could not ask to report an error: the design tokens are not loaded";
        return {};
    }
    ErrorReportDialog dialog(window_in_front(), *theme, report);
    Answer answer;
    if (dialog.ShowModal() == wxID_OK) {
        answer.report        = true;
        answer.automatically = dialog.m_automatic->is_checked();
    }
    return answer;
}

ErrorReportDialog* ErrorReportDialog::current() { return g_open; }

ErrorReportDialog::ErrorReportDialog(wxWindow* parent, const ShellTheme& theme, const ErrorReport& report)
    : wxDialog(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    , m_theme(theme)
{
    g_open = this;
    SetName("Error report");
    const bool          dark    = wxGetApp().dark_mode();
    const ShellPalette& palette = theme.palette(dark);
    const ShellMetrics& m       = theme.metrics();
    SetBackgroundColour(palette.surface_raised);
    const int text_width = FromDIP(kWidth - 2 * 24);

    auto* title = new wxStaticText(this, wxID_ANY, _L("Something went wrong on our side"));
    style_label(*title, theme, TextRole::Section, palette.text_primary);
    auto* body = new wxStaticText(this, wxID_ANY,
                                  _L("You can report this to the JusPrin team so they can fix it. The report is anonymous: it has "
                                     "no chat messages, no files and nothing that says who you are."));
    style_label(*body, theme, TextRole::Body, palette.text_secondary);
    body->Wrap(text_width);

    // "See what the report contains": closed until clicked.
    auto* disclosure = new wxPanel(this);
    disclosure->SetBackgroundColour(palette.surface_raised);
    const int icon = FromDIP(16);
    m_chevron      = new wxStaticBitmap(disclosure, wxID_ANY, lucide_bitmap("chevron-right", icon, palette.text_secondary));
    auto* disclosure_label = new wxStaticText(disclosure, wxID_ANY, _L("See what the report contains"));
    style_label(*disclosure_label, theme, TextRole::LabelBold, palette.text_secondary);
    auto* disclosure_row = new wxBoxSizer(wxHORIZONTAL);
    disclosure_row->Add(m_chevron, 0, wxALIGN_CENTER_VERTICAL);
    disclosure_row->Add(disclosure_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    disclosure->SetSizer(disclosure_row);
    disclosure->SetCursor(wxCursor(wxCURSOR_HAND));
    for (wxWindow* part : {static_cast<wxWindow*>(disclosure), static_cast<wxWindow*>(m_chevron), static_cast<wxWindow*>(disclosure_label)})
        part->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) { toggle_contents(); });
    m_contents = build_contents(report);
    m_contents->Hide();

    m_automatic = new CheckToggle(this, theme, _L("From now on, report errors like this automatically"));
    m_automatic->set_dark(dark);
    m_automatic->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { update_report_label(); });
    auto* automatic_detail = new wxStaticText(this, wxID_ANY,
                                              _L("Each report holds only the lines under ‘See what the report contains’. You can "
                                                 "turn this off in Preferences › Updates."));
    style_label(*automatic_detail, theme, TextRole::Label, palette.text_secondary);
    // Under the box's words, past the box itself.
    const int detail_indent = FromDIP(m.space_2 + m.checkbox.size + m.checkbox.label_gap);
    automatic_detail->Wrap(text_width - detail_indent);

    m_cancel = new Button(this, _L("Cancel"));
    style_button(*m_cancel, theme, palette, m.button.secondary);
    m_report = new Button(this, wxEmptyString);
    style_primary_button(*m_report, theme, palette, m.button.primary);
    update_report_label();
    m_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });
    m_report->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_OK); });
    SetEscapeId(wxID_CANCEL);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer();
    buttons->Add(m_cancel, 0, wxALIGN_CENTER_VERTICAL);
    buttons->Add(m_report, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

    auto* column = new wxBoxSizer(wxVERTICAL);
    column->Add(title, 0);
    column->Add(body, 0, wxTOP, FromDIP(16));
    column->Add(disclosure, 0, wxTOP, FromDIP(16));
    column->Add(m_contents, 0, wxEXPAND | wxTOP, FromDIP(8));
    column->Add(m_automatic, 0, wxEXPAND | wxTOP, FromDIP(16));
    column->Add(automatic_detail, 0, wxLEFT, detail_indent);
    column->Add(buttons, 0, wxEXPAND | wxTOP, FromDIP(16));
    column->SetMinSize(FromDIP(wxSize(kWidth - 2 * 24, -1)));
    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(column, 1, wxEXPAND | wxALL, FromDIP(24));
    SetSizerAndFit(outer);
    CentreOnParent();
    m_report->SetFocus();
}

ErrorReportDialog::~ErrorReportDialog()
{
    if (g_open == this)
        g_open = nullptr;
}

wxPanel* ErrorReportDialog::build_contents(const ErrorReport& report)
{
    const ShellPalette& palette = m_theme.palette(wxGetApp().dark_mode());
    auto* contents = new wxPanel(this);
    contents->SetBackgroundColour(palette.surface_raised);
    auto* box = new wxPanel(contents);
    box->SetBackgroundColour(palette.surface_subtle);

    const int value_width = FromDIP(kWidth - 2 * 24 - 2 * 12 - kNameWidth - 8);
    auto*     rows        = new wxFlexGridSizer(2, FromDIP(8), FromDIP(8));
    rows->AddGrowableCol(1);
    const auto add_row = [&](const wxString& name, const wxString& value) {
        auto* name_label = new wxStaticText(box, wxID_ANY, name);
        style_label(*name_label, m_theme, TextRole::LabelBold, palette.text_secondary);
        name_label->SetMinSize(wxSize(FromDIP(kNameWidth), -1));
        auto* value_label = new wxStaticText(box, wxID_ANY, wxEmptyString);
        value_label->SetFont(m_theme.code_font());
        value_label->SetForegroundColour(palette.text_primary);
        value_label->SetLabelText(wrap_anywhere(value, m_theme.code_font(), value_width, *value_label));
        rows->Add(name_label, 0);
        rows->Add(value_label, 1, wxEXPAND);
    };
    // The report's lines as the server receives them, in their own words:
    // a report is read by the team, in English.
    add_row(_L("App"), wxString::FromUTF8(report.app + " on " + report.os));
    add_row(_L("Was doing"), wxString::FromUTF8(report.doing));
    add_row(_L("Error"), wxString::FromUTF8(report.error));
    int where_lines = 0;
    if (!report.where.empty()) {
        where_lines = 1 + int(std::count(report.where.begin(), report.where.end(), '\n'));
        add_row(_L("Where"),
                // TRN %d is how many lines of the stack the report sends, such as 9.
                wxString::Format(_L_PLURAL("%d line naming the parts of JusPrin that were running",
                                           "%d lines naming the parts of JusPrin that were running", where_lines),
                                 where_lines));
    }
    auto* padded = new wxBoxSizer(wxVERTICAL);
    padded->Add(rows, 1, wxEXPAND | wxALL, FromDIP(12));
    box->SetSizer(padded);

    const wxString note = where_lines == 0 ?
        _L("That is the whole report. It has no chat messages, no files, no project names and no account or email address.") :
        wxString::Format(
            // TRN %d is how many lines of the stack the report sends, such as 9.
            _L_PLURAL("That line is not shown in full here. The report has no chat messages, no files, no project names and no "
                      "account or email address.",
                      "Those %d lines are not shown in full here. The report has no chat messages, no files, no project names "
                      "and no account or email address.",
                      where_lines),
            where_lines);
    auto* note_label = new wxStaticText(contents, wxID_ANY, note);
    style_label(*note_label, m_theme, TextRole::Label, palette.text_secondary);
    note_label->Wrap(FromDIP(kWidth - 2 * 24));

    auto* column = new wxBoxSizer(wxVERTICAL);
    column->Add(box, 0, wxEXPAND);
    column->Add(note_label, 0, wxTOP, FromDIP(8));
    contents->SetSizer(column);
    return contents;
}

void ErrorReportDialog::toggle_contents()
{
    const bool          open    = !m_contents->IsShown();
    const ShellPalette& palette = m_theme.palette(wxGetApp().dark_mode());
    m_contents->Show(open);
    m_chevron->SetBitmap(lucide_bitmap(open ? "chevron-down" : "chevron-right", FromDIP(16), palette.text_secondary));
    Layout();
    Fit();
}

void ErrorReportDialog::update_report_label()
{
    m_report->SetLabel(m_automatic->is_checked() ? _L("Report this and future errors") : _L("Report to JusPrin team"));
    m_report->Rescale();
    Layout();
}

} // namespace Slic3r::GUI::JusPrin
