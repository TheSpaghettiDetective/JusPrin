#pragma once

#include "CheckPrintReportModel.hpp"
#include "ShellTheme.hpp"

#include <wx/bitmap.h>
#include <wx/control.h>
#include <wx/panel.h>

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class wxGraphicsContext;

namespace Slic3r::GUI::JusPrin {

// The Check print report: a summary of time and material, then a collapsible
// section per check that has something to say, then one line naming the
// checks that found nothing. Read-only apart from selection: pressing a
// finding selects it and tells the owner, which moves the view. There are no
// buttons, because the way back to Prepare is closing the window.
class CheckPrintReportPanel final : public wxPanel
{
public:
    // The words the summary uses that the report does not carry.
    struct Context
    {
        // The filament preset of each extruder, by slice_report's zero-based number.
        std::vector<std::string> filament_names;
        // ISO 4217 code, empty when the OS does not say.
        std::string              currency;
    };
    // Section and row index of a finding in view().sections.
    using RowRef = std::pair<std::size_t, std::size_t>;

    CheckPrintReportPanel(wxWindow* parent, const ShellTheme& theme);

    void set_report(CheckPrintReportView view, Context context);
    const CheckPrintReportView& view() const { return m_view; }
    void set_dark(bool dark);

    // Called with the row the person pressed or reached with the arrow keys.
    void on_select(std::function<void(const CheckRow&)> callback) { m_on_select = std::move(callback); }
    // Selects as a press would, callback included.
    void select(RowRef row);
    const std::optional<RowRef>& selected() const { return m_selected; }
    // Where a row is in the panel now, scroll applied; empty when it is not
    // drawn (its section is collapsed).
    std::optional<wxRect> row_rect(RowRef row) const;
    int scroll() const { return m_scroll; }
    int content_height() const { return m_content_height; }
    // The scrollbar's track and thumb, in panel coordinates; empty when the
    // report fits.
    std::optional<std::pair<wxRect, wxRect>> scrollbar() const;
    void toggle_section(std::size_t section);
    bool is_collapsed(std::size_t section) const;

    // What the last drawing put on screen, line by line and in order: the
    // shell harness reads it to check the report without a screen capture.
    const std::vector<wxString>& drawn_lines() const { return m_drawn_lines; }
    // Draws the whole report, unscrolled, into a bitmap `width` wide and as
    // tall as the content, with the same code the paint event uses.
    wxBitmap snapshot(int width);
    // Draws the panel as it is on screen, scroll and scrollbar included.
    wxBitmap snapshot_view();

    // The words the panel draws, exposed for the canvas caption and the harness.
    static wxString section_title(PrintCheck check);
    static wxString closing_line(const CheckPrintReportView& view);
    wxString        row_message(const CheckRow& row) const;
    wxString        row_detail(const CheckRow& row) const;

private:
    struct Hit
    {
        enum class Kind { Header, Row } kind{Kind::Row};
        wxRect      rect;
        std::size_t section{0};
        std::size_t row{0};
    };

    void paint(wxPaintEvent&);
    void draw_view(wxGraphicsContext& gc);
    // Returns the height drawn, from `top`.
    int  draw(wxGraphicsContext& gc, int top, int width);
    int  draw_summary(wxGraphicsContext& gc, int x, int y, int width);
    int  draw_section(wxGraphicsContext& gc, std::size_t index, int x, int y, int width);
    int  draw_row(wxGraphicsContext& gc, const CheckRow& row, bool selected, bool hovered, int x, int y, int width);
    int  draw_closing(wxGraphicsContext& gc, int x, int y, int width);
    int  draw_wrapped(wxGraphicsContext& gc, const wxString& text, TextRole role, const wxColour& colour, int x, int y, int width);
    wxString section_count(const CheckSection& section) const;

    void on_left_down(wxMouseEvent& event);
    void on_motion(wxMouseEvent& event);
    void on_wheel(wxMouseEvent& event);
    void on_key(wxKeyEvent& event);
    void clamp_scroll();
    // Lays the report out again at the panel's width, for the hit areas and
    // the content height, without waiting for a paint.
    void layout_now();
    void scroll_into_view(RowRef row);
    std::vector<RowRef> visible_rows() const;

    const ShellTheme&                       m_theme;
    bool                                    m_dark{false};
    CheckPrintReportView                    m_view;
    Context                                 m_context;
    std::vector<bool>                       m_collapsed;
    std::optional<RowRef>                   m_selected;
    std::optional<RowRef>                   m_hovered;
    std::function<void(const CheckRow&)>    m_on_select;
    std::vector<Hit>                        m_hits;
    std::vector<wxString>                   m_drawn_lines;
    int                                     m_scroll{0};
    int                                     m_content_height{0};
};

// The window header's two switches: the token checkbox and its label, one
// focus target that raises wxEVT_CHECKBOX when clicked or when Space is
// pressed on it.
class CheckToggle final : public wxControl
{
public:
    CheckToggle(wxWindow* parent, const ShellTheme& theme, const wxString& label);
    bool is_checked() const { return m_checked; }
    // Sets the state without raising the event.
    void set_checked(bool checked);
    void set_dark(bool dark);
    // Toggles and raises the event, as a click does.
    void toggle();
    wxSize DoGetBestSize() const override;
    bool   AcceptsFocus() const override { return true; }
    wxBitmap snapshot();

private:
    void paint(wxPaintEvent&);
    void draw(wxGraphicsContext& gc, const wxSize& size);

    const ShellTheme& m_theme;
    bool              m_dark{false};
    bool              m_checked{false};
    bool              m_hovered{false};
};

} // namespace Slic3r::GUI::JusPrin
