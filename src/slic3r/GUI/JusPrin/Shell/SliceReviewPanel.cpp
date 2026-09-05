#include "SliceReviewPanel.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/I18N.hpp"
#include <wx/dcbuffer.h>
#include <wx/textwrapper.h>
#include <wx/weakref.h>
#include <algorithm>

namespace Slic3r::GUI::JusPrin {
namespace {
class WrappedText : public wxTextWrapper {
public:
    std::vector<wxString> lines;
    void OnOutputLine(const wxString& line) override { lines.push_back(line); }
};
}

SliceReviewPanel::SliceReviewPanel(wxWindow* parent, const ShellTheme& theme, Displayed displayed)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE),
      m_theme(theme), m_displayed(std::move(displayed))
{
    SetName("Slice review findings");
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetFont(Label::Body_14);
    SetMinSize(FromDIP(wxSize(-1,120)));
    SetScrollRate(0,FromDIP(16));
    Bind(wxEVT_PAINT, &SliceReviewPanel::paint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { Refresh(); e.Skip(); });
}

void SliceReviewPanel::set_report(Workspace::SliceIdentity identity, std::vector<std::string> findings)
{
    if (identity == m_identity && findings == m_findings) return;
    m_identity = identity;
    m_findings = std::move(findings);
    m_seen.clear();
    m_notified = false;
    wxString accessible;
    for (const auto& finding : m_findings) accessible += wxString::FromUTF8(finding) + "\n";
    SetLabel(accessible);
    Scroll(0,0);
    Refresh();
}

void SliceReviewPanel::paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    const auto& palette = m_theme.palette(m_dark);
    dc.SetBackground(wxBrush(palette.surface_subtle)); dc.Clear();
    PrepareDC(dc);
    dc.SetTextForeground(palette.text_primary);
    dc.SetFont(Label::Head_12);
    const int margin = FromDIP(12);
    int y = margin;
    dc.DrawText(_L("Check print"),margin,y);
    y += dc.GetCharHeight()+FromDIP(8);
    dc.SetFont(GetFont());
    int scroll_x, scroll_y;
    CalcUnscrolledPosition(0,0,&scroll_x,&scroll_y);
    if (m_findings.empty()) {
        dc.DrawText(_L("No reported findings. Inspect the sliced result before printing."),margin,y);
        y += dc.GetCharHeight();
    }
    std::vector<wxString> lines;
    for (size_t i=0;i<m_findings.size();++i) {
        WrappedText wrapped;
        wrapped.Wrap(this,wxString::Format("%d. ",int(i+1))+wxString::FromUTF8(m_findings[i]),
                     std::max(FromDIP(40),GetClientSize().x-2*margin));
        lines.insert(lines.end(),wrapped.lines.begin(),wrapped.lines.end());
    }
    if (m_seen.size() != lines.size()) m_seen.assign(lines.size(),false);
    const int line_height = dc.GetCharHeight()+FromDIP(4);
    for (size_t i=0;i<lines.size();++i) {
        dc.DrawText(lines[i],margin,y);
        // Scrolling to the last finding must not count skipped lines as read.
        if (y >= scroll_y && y+line_height <= scroll_y+GetClientSize().y) m_seen[i] = true;
        y += line_height;
    }
    SetVirtualSize(wxSize(GetClientSize().x,y+margin));
    if (!m_notified && !m_findings.empty() && IsShownOnScreen() &&
        std::all_of(m_seen.begin(),m_seen.end(),[](bool seen) { return seen; })) {
        m_notified = true;
        CallAfter([self=wxWeakRef<SliceReviewPanel>(this),identity=m_identity,findings=m_findings] {
            if (self && self->m_identity == identity && self->m_findings == findings)
                self->m_notified = self->IsShownOnScreen() && self->m_displayed(identity,findings);
        });
    }
}

} // namespace Slic3r::GUI::JusPrin
