#pragma once

#include "ShellTheme.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SliceReview.hpp"
#include <wx/scrolwin.h>
#include <functional>

namespace Slic3r::GUI::JusPrin {

// The report remains next to the real Preview. Acknowledgement is evidence of
// displaying every finding, not merely selecting the Preview notebook page.
class SliceReviewPanel : public wxScrolledWindow
{
public:
    using Displayed = std::function<bool(Workspace::SliceIdentity, const std::vector<std::string>&)>;
    SliceReviewPanel(wxWindow* parent, const ShellTheme& theme, Displayed displayed);
    void set_report(Workspace::SliceIdentity identity, std::vector<std::string> findings);
    void set_dark(bool dark) { m_dark = dark; Refresh(); }
private:
    void paint(wxPaintEvent&);
    const ShellTheme& m_theme;
    Displayed m_displayed;
    Workspace::SliceIdentity m_identity;
    std::vector<std::string> m_findings;
    std::vector<bool> m_seen;
    bool m_dark{false}, m_notified{false};
};

} // namespace Slic3r::GUI::JusPrin
