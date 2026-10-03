#pragma once

// What the Plates / Project pane shows, as plain data. No wx and no Orca types:
// the rows are a pure function of the workspace's outline and snapshot plus a
// little navigation state, so every rule about what appears, and when, is
// tested without a window. The pane (LeftPane) only paints these rows.

#include "slic3r/GUI/JusPrin/Workspace/ProjectOutline.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin {

enum class PaneTab : std::uint8_t { Plates, Project };

// Views inside the Project tab. Root is the tab itself; every other view is one
// level below it and returns to it.
enum class ProjectView : std::uint8_t { Root, Details, PrintHistory, Versions };

// Which tab and view are showing, and which objects the person has opened.
// The expansion set is project-scoped, so it is dropped whenever the project
// session changes, and a new project always starts on the Project root.
class PaneNavigation
{
public:
    PaneTab     tab() const { return m_tab; }
    ProjectView view() const { return m_view; }

    // A tab always lands on its top level, so Plates is the plate list and
    // Project is the Project root, whatever was showing before.
    void show_plates() { m_tab = PaneTab::Plates; m_view = ProjectView::Root; }
    void show_project() { m_tab = PaneTab::Project; m_view = ProjectView::Root; }

    void open(ProjectView view) { m_tab = PaneTab::Project; m_view = view; }
    // Goes up one Project level. False when there is nowhere to go up to, so
    // Escape at the Project root, or on the Plates tab, is left alone.
    bool back()
    {
        if (m_tab != PaneTab::Project || m_view == ProjectView::Root)
            return false;
        m_view = ProjectView::Root;
        return true;
    }

    bool is_expanded(Workspace::ObjectId id) const { return m_expanded.count(id) != 0; }
    void set_expanded(Workspace::ObjectId id, bool expanded)
    {
        if (expanded)
            m_expanded.insert(id);
        else
            m_expanded.erase(id);
    }
    void toggle_expanded(Workspace::ObjectId id) { set_expanded(id, !is_expanded(id)); }

    // Called with every snapshot. Returns true when the project changed.
    bool follow_session(Workspace::ProjectSessionId session)
    {
        if (session == m_session)
            return false;
        m_session = session;
        m_expanded.clear();
        m_view = ProjectView::Root;
        return true;
    }

private:
    PaneTab                       m_tab{PaneTab::Plates};
    ProjectView                   m_view{ProjectView::Root};
    Workspace::ProjectSessionId   m_session;
    std::set<Workspace::ObjectId> m_expanded;
};

struct PaneRow
{
    enum class Kind : std::uint8_t { Plate, Object, Copy, Volume, OffPlateHeader };

    Kind kind{Kind::Plate};
    int  depth{0};

    // Identity: the field that matches `kind` is set. An object row also
    // carries its plate, since the same object can be listed on several.
    Workspace::PlateId    plate;
    Workspace::ObjectId   object;
    Workspace::InstanceId copy;
    Workspace::VolumeId   volume;

    std::string name;
    bool        selected{false};
    bool        expandable{false};
    bool        expanded{false};

    // Plate rows.
    bool                    sliced{false};
    bool                    locked{false};
    bool                    custom_settings{false};
    Workspace::PlateSummary summary; // meaningful on a folded plate only

    // Object rows: the copies listed under this row, and how many of them
    // will not print. A disabled object makes all of them not print.
    std::size_t copies{0};
    std::size_t wont_print{0};
    bool        object_printable{true};
    std::optional<int> filament; // 1-based; set only when the assignment is worth showing
    std::string        filament_colour; // that slot's "#RRGGBB", when the profile names one

    // Copy rows: 1-based position among the object's copies, as listed.
    std::size_t ordinal{0};
    bool        copy_wont_print{false};
    bool        copy_printable{true};

    // Volume rows.
    Workspace::VolumeRole role{Workspace::VolumeRole::Part};

    // Object and volume rows. A customization is a choice the person made; a
    // mesh problem is a defect in the model. They never share a mark.
    bool                    customized{false};
    Workspace::ObjectCustomization customization;
    Workspace::MeshProblem  mesh;

    bool all_wont_print() const { return copies != 0 && wont_print == copies; }
};

// The Plates tab, top to bottom: every plate in order; under the active plate
// its objects, and under each object only the children that exist; then the
// group of copies no plate holds, when there are any.
std::vector<PaneRow> build_plate_rows(const Workspace::ProjectOutline&  outline,
                                      const Workspace::WorkspaceSnapshot& snapshot,
                                      const PaneNavigation&              navigation);

} // namespace Slic3r::GUI::JusPrin
