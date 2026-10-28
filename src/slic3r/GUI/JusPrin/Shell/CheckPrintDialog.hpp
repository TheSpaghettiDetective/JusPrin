#pragma once

#include "CheckPrintReportModel.hpp"
#include "ShellTheme.hpp"

#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/JusPrin/Workspace/ProjectState.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <wx/dialog.h>
#include <wx/timer.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class wxStaticText;

namespace Slic3r::GUI {
class Preview;
}

namespace Slic3r::GUI::JusPrin {

class CheckPrintReportPanel;
class CheckToggle;
class HeaderButton;
class PreviewPresentationController;

// The Check print window: OrcaSlicer's own G-code preview of the active plate
// with a report of the slice beside it, modal over Prepare. A header row
// holds the title, what was sliced, the Legend switch (off when it opens,
// not remembered) and the close button. Closing it,
// or Escape, is the only way back; it also closes itself as soon as the slice
// it shows is no longer the current one.
class CheckPrintDialog final : public wxDialog
{
public:
    // `project_name` is the name the window title shows; the header repeats it.
    CheckPrintDialog(wxWindow* parent, const ShellTheme& theme, Plater& plater, Workspace::IWorkspace& workspace,
                     wxString project_name, std::vector<Workspace::RegionRecord> regions, std::function<bool()> is_current);
    ~CheckPrintDialog() override;

    // What the window says about the selected finding on the canvas.
    struct Caption
    {
        wxString    title;
        std::size_t layer{0}; // counted from one
        double      z{0};
    };

    // For the shell harness: the parts it drives and reads.
    Preview*               preview() const { return m_preview; }
    CheckPrintReportPanel* report() const { return m_report; }
    CheckToggle*           legend_toggle() const { return m_legend; }
    HeaderButton*          close_button() const { return m_close; }
    const wxStaticText*    context_label() const { return m_context; }
    bool                   legend_shown() const;
    const std::optional<Caption>& caption() const { return m_caption; }
    // Where the last frame drew the caption, in canvas pixels, and whether its
    // title had the bold font; empty when no caption was drawn.
    const std::optional<wxRect>& caption_rect() const { return m_caption_rect; }
    bool                         caption_bold() const { return m_caption_bold; }
    // The Tab order: the canvas, the report, the Legend switch, Close.
    std::vector<wxWindow*> focus_order() const;
    // The control after `from` in that order, wrapping; the first (or, going
    // back, the last) when `from` is not in it.
    wxWindow*              next_focus(const wxWindow* from, bool backward) const;
    void                   move_focus(bool backward);

private:
    wxWindow* build_header(const ShellPalette& palette);
    wxString context_text() const;
    void     fill_report();
    void     show_finding(const CheckRow& row);
    void     on_frame();
    void     draw_caption();
    void     forget_pointer();
    void     close_if_stale();
    void     on_slice_status_changed(wxCommandEvent& event);

    const ShellTheme&                              m_theme;
    Plater&                                        m_plater;
    Workspace::IWorkspace&                         m_workspace;
    wxString                                       m_project_name;
    std::vector<Workspace::RegionRecord>           m_regions;
    std::function<bool()>                          m_is_current;
    Preview*                                       m_preview{nullptr};
    CheckPrintReportPanel*                         m_report{nullptr};
    CheckToggle*                                   m_legend{nullptr};
    wxStaticText*                                  m_context{nullptr};
    HeaderButton*                                  m_close{nullptr};
    std::unique_ptr<PreviewPresentationController> m_presentation;
    std::optional<Caption>                         m_caption;
    std::optional<wxRect>                          m_caption_rect;
    bool                                           m_caption_bold{false};
    bool                                           m_fit_pending{false};
    // Prepare and the preview share OrcaSlicer's camera: what it was when the
    // window opened, put back when it closes.
    std::optional<Camera>                          m_camera_before;
    ProjectStateSubscription                       m_subscription;
    wxTimer                                        m_stale_timer{this};
};

} // namespace Slic3r::GUI::JusPrin
