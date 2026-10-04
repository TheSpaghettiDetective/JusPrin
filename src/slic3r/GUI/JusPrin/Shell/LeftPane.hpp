#pragma once

#include "HeaderControls.hpp"
#include "LeftPaneModel.hpp"
#include "ShellTheme.hpp"

#include "ProjectPaneModel.hpp"

#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"
#include "slic3r/GUI/JusPrin/Workspace/ProjectVersionStore.hpp"

#include <wx/bitmap.h>
#include <wx/panel.h>

#include <functional>
#include <chrono>
#include <memory>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

class wxGraphicsContext;

namespace Slic3r::GUI::JusPrin::Agent {
class ProjectPersistence;
}
namespace Slic3r::GUI::JusPrin::Workspace {
class ProjectAutosave;
}

namespace Slic3r::GUI::JusPrin {

// What the Project tab reads and calls. All of it is owned elsewhere; the pane
// holds no copy of any of it, and a tab that has none of it shows what it can.
struct ProjectSources
{
    Agent::ProjectPersistence*  persistence{nullptr};
    Workspace::ProjectAutosave* autosave{nullptr};
    // Opens OrcaSlicer's own project-information editor.
    std::function<void()>       edit_project_info;
    // Asks whether to restore a version; unset asks with a dialog.
    std::function<bool()>       confirm_restore;
};

// The Plates / Project pane, left of the canvas. It paints what LeftPaneModel
// describes and sends every click to a workspace command. Its version list is
// a view cache; project facts still come from their owning stores.
//
// Painting records where each control landed, and the mouse and keyboard read
// that record: what a click hits is, by construction, what was drawn.
class LeftPane : public wxPanel
{
public:
    LeftPane(wxWindow* parent, const ShellTheme& theme, Workspace::IWorkspace& workspace);
    ~LeftPane() override;

    void apply_appearance(bool dark);
    // The open pane owns one copy of its toggle; ShellController keeps the
    // collapsed copy in the project header so the control stays at x=16.
    void set_pane_toggle(std::function<void()> toggle);
    void set_project_sources(ProjectSources sources);
    // Replaces the restore question, as the header's does for the harness.
    void set_restore_confirmation(std::function<bool()> confirmation) { m_sources.confirm_restore = std::move(confirmation); }
    // The Project tab's actions, shared by the pointer and the harness.
    void open_project_view(ProjectView view);
    void export_project();
    void restore_version(const std::string& version_id);
    // Re-reads the workspace and repaints. Called for every workspace change.
    void refresh_from_workspace();
    void refresh_slicing_progress();

    PaneNavigation&       navigation() { return m_navigation; }
    const PaneNavigation& navigation() const { return m_navigation; }
    // The rows the Plates tab currently shows, for the integration harness.
    const std::vector<PaneRow>& rows() const { return m_rows; }

    void show_plates();
    void show_project();
    // Escape: goes up one Project level. False when it is not the pane's.
    bool handle_escape();

    // What a click on a row does, shared by the pointer, the keyboard and the
    // harness so none of them has a path of its own.
    void activate_row(const PaneRow& row);
    void toggle_row(const PaneRow& row);
    void open_row_menu(const PaneRow& row);
    void add_plate();

    // Offers the pane the keys it handles. Returns true when it used one.
    bool handle_key(int key_code);

    // Where a drawn control last landed, in this window's coordinates, or an
    // empty rectangle when it is not on screen. The pointer reads the same
    // record, so the integration harness aims its clicks with these.
    enum class Part { Row, Expander, Overflow, Mark };
    wxRect rect_of(std::size_t row, Part part) const;
    wxRect tab_rect(PaneTab tab) const;
    wxRect add_plate_rect() const;
    // The Project tab's controls as drawn, in order, and a way to press one by
    // its label -- the same record the pointer reads.
    std::vector<wxString> action_labels() const;
    bool                  press_action(const wxString& label);
    wxRect                action_rect(const wxString& label) const;
    // Draws the pane into a bitmap with the code the paint event uses, so its
    // appearance can be checked from inside the running application.
    wxBitmap snapshot();

    wxSize DoGetBestSize() const override;
    bool   AcceptsFocus() const override { return true; }

private:
    // One drawn control: where it is, and what it does.
    struct Hit
    {
        enum class Kind : std::uint8_t { Tab, Row, Expander, Overflow, AddPlate, Action, Mark };
        Kind       kind{Kind::Row};
        wxRect     rect;
        std::size_t row{0};  // index into m_rows for the row kinds
        PaneTab    tab{PaneTab::Plates};
        // Action: what the control says it does, and does.
        wxString                label;
        std::function<void()>   invoke;
    };

    void paint(wxPaintEvent&);
    void draw(wxGraphicsContext& gc, const wxSize& client);
    void draw_tabs(wxGraphicsContext& gc, int width);
    void layout_header();
    int  draw_plates(wxGraphicsContext& gc, int top, int width);
    // LeftPaneProject.cpp: the Project tab. Each returns the y below what it drew.
    int  draw_project(wxGraphicsContext& gc, int top, int width);
    int  draw_project_root(wxGraphicsContext& gc, int y, int width);
    int  draw_project_details(wxGraphicsContext& gc, int y, int width);
    int  draw_print_history(wxGraphicsContext& gc, int y, int width);
    int  draw_versions(wxGraphicsContext& gc, int y, int width);
    int  draw_back_row(wxGraphicsContext& gc, int y, int width, const wxString& title);
    int  draw_action_row(wxGraphicsContext& gc, int y, int width, const wxString& label, const wxString& detail,
                         bool chevron, std::function<void()> invoke, bool enabled = true,
                         HeaderIcon glyph = HeaderIcon::Right);
    // The model's own pictures (the "Model Pictures" attachments), up to `count`
    // across the pane. Draws nothing when there are none.
    int  draw_cover(wxGraphicsContext& gc, int y, int width, std::size_t count);
    int  draw_view_title(wxGraphicsContext& gc, int y, int width, const wxString& title);
    int  draw_disclosure_row(wxGraphicsContext& gc, int y, int width, const std::string& key, const wxString& label,
                             const wxString& detail);
    int  draw_paragraph(wxGraphicsContext& gc, int y, int width, const wxString& text, TextRole role, const wxColour& colour,
                        int indent = 0);
    int  draw_section_title(wxGraphicsContext& gc, int y, int width, const wxString& text);
    int  draw_outline_button(wxGraphicsContext& gc, int y, int width, const wxString& label, std::function<void()> invoke,
                             bool leading_plus = false);
    void draw_plate_row(wxGraphicsContext& gc, const PaneRow& row, std::size_t index, int& y, int width, int block_height);
    void draw_object_row(wxGraphicsContext& gc, const PaneRow& row, std::size_t index, int& y, int width);

    void on_left_down(wxMouseEvent& event);
    void on_motion(wxMouseEvent& event);
    void on_leave(wxMouseEvent& event);
    void on_wheel(wxMouseEvent& event);
    void on_right_down(wxMouseEvent& event);
    void on_key_down(wxKeyEvent& event);

    const Hit* hit_at(const wxPoint& point, std::initializer_list<Hit::Kind> kinds) const;
    void       run_command(const Workspace::CommandResult& result, const char* what) const;
    void place_menu_anchor(std::size_t index);
    void open_menu(std::size_t index, std::vector<HeaderMenuItem> items);
    std::vector<HeaderMenuItem> plate_menu(const PaneRow& row);
    std::vector<HeaderMenuItem> object_menu(const PaneRow& row);
    std::vector<HeaderMenuItem> copy_menu(const PaneRow& row);
    std::vector<HeaderMenuItem> volume_menu(const PaneRow& row);
    std::vector<HeaderMenuItem> customization_menu(const PaneRow& row);
    // The one gray mark a customized object or plate carries. Returns its width.
    int  draw_customization_mark(wxGraphicsContext& gc, std::size_t index, int right, int row_top, int row_height);
    void show_filament_choices(Workspace::ObjectId object);
    void show_split_choices(Workspace::ObjectId object);
    void       move_focus(int delta);
    int        content_height() const { return m_content_height; }
    void       clamp_scroll();

    wxString plate_status(const PaneRow& row) const;
    wxString plate_summary(const PaneRow& row) const;

    const ShellTheme&       m_theme;
    Workspace::IWorkspace&  m_workspace;
    Workspace::WorkspaceSubscription m_subscription;
    PaneNavigation          m_navigation;
    ProjectSources          m_sources;
    Workspace::WorkspaceSubscription m_ledger_subscription;
    // The model metadata and its attachments, read when the Project tab draws
    // and kept until the workspace next changes.
    std::optional<Workspace::ProjectDetails> m_details;
    // Loaded when Version history opens, never from a paint event.
    std::vector<Workspace::ProjectVersionStore::Version> m_versions;
    // Which folded rows of the Details view the person has opened.
    std::set<std::string> m_open_sections;
    // Decoded pictures by file, size and modification time, so a repaint reads no file.
    std::map<std::string, wxBitmap> m_pictures;
    bool                    m_dark{false};

    Workspace::WorkspaceSnapshot m_snapshot;
    std::chrono::steady_clock::time_point m_last_progress_refresh{};
    Workspace::ProjectOutline    m_outline;
    std::vector<PaneRow>         m_rows;

    // The record the pointer reads; rebuilt by every paint.
    std::vector<Hit> m_hits;
    int  m_scroll{0};
    int  m_content_height{0};
    int  m_hover_row{-1};
    int  m_focus_row{-1};
    // The focus ring shows for keyboard navigation only, as for any native control.
    bool m_focus_visible{false};
    bool m_tab_focus_visible{false};
    // The Project tab's keyboard position, among its drawn controls in order.
    int  m_action_focus{-1};
    std::vector<std::size_t> action_hits() const;
    void draw_focus_ring(wxGraphicsContext& gc, std::size_t index, const wxRect& rect);

    // Anchor for popup menus: HeaderMenu opens under a HeaderButton.
    HeaderButton* m_menu_anchor{nullptr};
    HeaderButton* m_pane_toggle{nullptr};
    std::function<void()> m_pane_toggle_callback;
    // Keeps a menu's rows alive until the owner has run the chosen command.
    std::function<void()> m_pending_command;
};

} // namespace Slic3r::GUI::JusPrin
