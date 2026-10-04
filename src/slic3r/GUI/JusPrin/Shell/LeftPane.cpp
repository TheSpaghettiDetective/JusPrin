#include "LeftPane.hpp"
#include "SetupCommands.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>

#include <boost/log/trivial.hpp>

#include <algorithm>

namespace Slic3r::GUI::JusPrin {

using namespace Workspace;

namespace {

// Joining a wxString to a narrow literal decodes it through the locale's
// encoding, which mangles these on Windows; decode them explicitly instead.
const wxString& middle_dot() { static const wxString s = wxString::FromUTF8(" \xC2\xB7 "); return s; }
const wxString& times()      { static const wxString s = wxString::FromUTF8("\xC3\x97"); return s; }
const wxString& ellipsis()   { static const wxString s = wxString::FromUTF8("\xE2\x80\xA6"); return s; }

// Cuts `text` to `max_width`, ending it with an ellipsis, using the font the
// graphics context already carries.
wxString fit(wxGraphicsContext& gc, const wxString& text, double max_width)
{
    double width = 0, height = 0;
    gc.GetTextExtent(text, &width, &height);
    if (width <= max_width)
        return text;
    wxString cut = text;
    while (!cut.empty()) {
        cut.RemoveLast();
        gc.GetTextExtent(cut + ellipsis(), &width, &height);
        if (width <= max_width)
            return cut + ellipsis();
    }
    return ellipsis();
}

double text_width(wxGraphicsContext& gc, const wxString& text)
{
    double width = 0, height = 0;
    gc.GetTextExtent(text, &width, &height);
    return width;
}

} // namespace

LeftPane::LeftPane(wxWindow* parent, const ShellTheme& theme, IWorkspace& workspace)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS | wxTAB_TRAVERSAL),
      m_theme(theme), m_workspace(workspace)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetName("Plates and Project");
    m_menu_anchor = new HeaderButton(this, theme, HeaderStyle::Quiet, wxEmptyString, HeaderIcon::More);
    m_menu_anchor->Hide();
    m_pane_toggle = new HeaderButton(this, theme, HeaderStyle::Outline, wxEmptyString, HeaderIcon::PanelLeftOpen);
    m_pane_toggle->SetName(_L("Plates and Project panel"));
    m_pane_toggle->SetToolTip(_L("Hide the Plates and Project panel"));
    m_pane_toggle->Hide();
    m_pane_toggle->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (m_pane_toggle_callback)
            m_pane_toggle_callback();
    });

    Bind(wxEVT_PAINT, &LeftPane::paint, this);
    Bind(wxEVT_LEFT_DOWN, &LeftPane::on_left_down, this);
    Bind(wxEVT_RIGHT_DOWN, &LeftPane::on_right_down, this);
    Bind(wxEVT_MOTION, &LeftPane::on_motion, this);
    Bind(wxEVT_LEAVE_WINDOW, &LeftPane::on_leave, this);
    Bind(wxEVT_MOUSEWHEEL, &LeftPane::on_wheel, this);
    Bind(wxEVT_KEY_DOWN, &LeftPane::on_key_down, this);
    Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) { Refresh(); event.Skip(); });
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) { Refresh(); event.Skip(); });
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) { layout_header(); clamp_scroll(); Refresh(); event.Skip(); });

    m_subscription = m_workspace.subscribe([this](const WorkspaceChanged&) { refresh_from_workspace(); });
    refresh_from_workspace();
}

LeftPane::~LeftPane() = default;

wxSize LeftPane::DoGetBestSize() const
{
    return wxSize(FromDIP(m_theme.metrics().left_pane.width), wxDefaultCoord);
}

void LeftPane::apply_appearance(bool dark)
{
    m_dark = dark;
    m_pane_toggle->set_dark(dark);
    Refresh();
}

void LeftPane::set_pane_toggle(std::function<void()> toggle)
{
    m_pane_toggle_callback = std::move(toggle);
    m_pane_toggle->Show(static_cast<bool>(m_pane_toggle_callback));
    layout_header();
}

void LeftPane::layout_header()
{
    const int margin = FromDIP(m_theme.metrics().space_4);
    const wxSize size = m_pane_toggle->GetBestSize();
    const int header_height = FromDIP(2 * m_theme.metrics().left_pane.padding_y +
                                      m_theme.metrics().left_pane.tab_height);
    m_pane_toggle->SetSize(margin, (header_height - size.y) / 2, size.x, size.y);
}

void LeftPane::refresh_from_workspace()
{
    m_snapshot = m_workspace.snapshot();
    m_outline  = m_workspace.outline();
    m_details.reset();
    if (m_navigation.follow_session(m_snapshot.session)) {
        m_open_sections.clear();
        m_versions.clear();
    }
    m_rows = build_plate_rows(m_outline, m_snapshot, m_navigation);
    if (m_hover_row >= static_cast<int>(m_rows.size()))
        m_hover_row = -1;
    if (m_focus_row >= static_cast<int>(m_rows.size()))
        m_focus_row = static_cast<int>(m_rows.size()) - 1;
    Refresh();
}

void LeftPane::refresh_slicing_progress()
{
    const auto now = std::chrono::steady_clock::now();
    if (now - m_last_progress_refresh < std::chrono::milliseconds(200))
        return;
    m_last_progress_refresh = now;
    const auto slicing = m_workspace.snapshot().slicing;
    if (m_snapshot.slicing.plate != slicing.plate || m_snapshot.slicing.percent != slicing.percent ||
        m_snapshot.slicing.running != slicing.running) {
        m_snapshot.slicing = slicing;
        Refresh();
    }
}

void LeftPane::show_plates()
{
    m_navigation.show_plates();
    m_scroll       = 0;
    m_action_focus = -1;
    Refresh();
}

void LeftPane::show_project()
{
    m_navigation.show_project();
    m_scroll       = 0;
    m_action_focus = -1;
    Refresh();
}

bool LeftPane::handle_escape()
{
    if (!m_navigation.back())
        return false;
    m_scroll       = 0;
    m_action_focus = -1;
    Refresh();
    return true;
}

wxString LeftPane::plate_status(const PaneRow& row) const
{
    if (m_snapshot.slicing.running && m_snapshot.slicing.plate == row.plate)
        return m_snapshot.slicing.percent ? wxString::Format(_L("Slicing %d%%"), *m_snapshot.slicing.percent) : _L("Slicing…");
    return row.sliced ? _L("Sliced") : _L("Needs slice");
}

wxString LeftPane::plate_summary(const PaneRow& row) const
{
    const PlateSummary& summary = row.summary;
    if (summary.empty())
        return _L("Empty");
    wxString text;
    if (summary.objects == 1) {
        text = wxString::FromUTF8(summary.single_object);
        if (summary.copies > 1)
            text += " " + times() + wxString::Format("%d", int(summary.copies));
    } else {
        text = wxString::Format(_L("%d objects"), int(summary.objects));
    }
    if (summary.wont_print > 0)
        text += middle_dot() + wxString::Format(_L("%d won't print"), int(summary.wont_print));
    return text;
}

// -- Commands ---------------------------------------------------------------

void LeftPane::run_command(const CommandResult& result, const char* what) const
{
    // Choosing what is already chosen is not an error.
    if (result.succeeded() || result.error == WorkspaceError::NoChange)
        return;
    BOOST_LOG_TRIVIAL(error) << "Plates pane: " << what << " failed: " << result.message;
    wxMessageBox(wxString::FromUTF8(result.message), _L("Couldn't do that"), wxOK | wxICON_ERROR);
}

void LeftPane::activate_row(const PaneRow& row)
{
    switch (row.kind) {
    case PaneRow::Kind::Plate: run_command(m_workspace.select_plate(row.plate), "select plate"); break;
    case PaneRow::Kind::Object: run_command(m_workspace.select_object(row.object), "select object"); break;
    case PaneRow::Kind::Copy: run_command(m_workspace.select_copy(row.copy), "select copy"); break;
    case PaneRow::Kind::Volume: run_command(m_workspace.select_volume(row.volume), "select part"); break;
    case PaneRow::Kind::OffPlateHeader: break;
    }
}

void LeftPane::toggle_row(const PaneRow& row)
{
    if (row.kind != PaneRow::Kind::Object || !row.expandable)
        return;
    m_navigation.set_expanded(row.object, !row.expanded);
    refresh_from_workspace();
}

void LeftPane::add_plate()
{
    run_command(m_workspace.add_plate(), "add plate");
}

// A customization is a choice the person made, so it is one quiet mark, not a
// colour: a small diamond in the secondary text colour. A mesh problem is a
// defect in the model and is spelled out in the danger colour instead.
int LeftPane::draw_customization_mark(wxGraphicsContext& gc, std::size_t index, int right, int row_top, int row_height)
{
    const ShellPalette& p = m_theme.palette(m_dark);
    const int size  = FromDIP(m_theme.metrics().space_2);
    const int width = FromDIP(m_theme.metrics().left_pane.glyph_size) - FromDIP(m_theme.metrics().space_1);
    const double cx = right - width / 2.0;
    const double cy = row_top + row_height / 2.0;
    wxGraphicsPath path = gc.CreatePath();
    path.MoveToPoint(cx, cy - size / 2.0);
    path.AddLineToPoint(cx + size / 2.0, cy);
    path.AddLineToPoint(cx, cy + size / 2.0);
    path.AddLineToPoint(cx - size / 2.0, cy);
    path.CloseSubpath();
    gc.SetPen(*wxTRANSPARENT_PEN);
    gc.SetBrush(wxBrush(p.text_secondary));
    gc.FillPath(path);
    // The object mark also explains setting overrides when no editing tool is
    // available for them in this shell.
    const PaneRow& row = m_rows[index];
    if (row.kind == PaneRow::Kind::Plate || row.customized)
        m_hits.push_back({Hit::Kind::Mark, wxRect(right - width, row_top, width, row_height), index, PaneTab::Plates});
    return width;
}

std::vector<HeaderMenuItem> LeftPane::customization_menu(const PaneRow& row)
{
    // Each customization the object carries, as the OrcaSlicer tool that made it.
    std::vector<HeaderMenuItem> items;
    const ObjectId object = row.object;
    const auto add = [&](const wxString& label, CustomizationTool tool) {
        HeaderMenuItem item;
        item.label  = label;
        item.invoke = [this, object, tool] { run_command(m_workspace.open_customization(object, tool), "open tool"); };
        items.push_back(std::move(item));
    };
    const ObjectCustomization& c = row.customization;
    if (c.support_painting) add(_L("Support painting"), CustomizationTool::SupportPainting);
    if (c.seam_painting) add(_L("Seam painting"), CustomizationTool::SeamPainting);
    if (c.color_painting) add(_L("Color painting"), CustomizationTool::ColorPainting);
    if (c.fuzzy_skin_painting) add(_L("Fuzzy skin painting"), CustomizationTool::FuzzySkinPainting);
    if (c.variable_layer_height) add(_L("Variable layer height"), CustomizationTool::VariableLayerHeight);
    if (c.setting_overrides > 0) {
        HeaderMenuItem settings;
        settings.label = wxString::Format(_L("Per-object settings · %d"), int(c.setting_overrides));
        settings.enabled = false;
        items.push_back(std::move(settings));
    }
    return items;
}

// Parks the hidden menu anchor where the row's overflow glyph is drawn (or at
// the row's right edge when it is not), so the popup opens beside the row.
void LeftPane::place_menu_anchor(std::size_t index)
{
    wxRect rect = rect_of(index, Part::Overflow);
    if (rect.IsEmpty()) {
        rect = rect_of(index, Part::Row);
        if (!rect.IsEmpty())
            rect = wxRect(rect.GetRight() - FromDIP(16), rect.y, FromDIP(16), rect.height);
    }
    m_menu_anchor->SetSize(rect);
}

void LeftPane::open_menu(std::size_t index, std::vector<HeaderMenuItem> items)
{
    if (items.empty())
        return;
    place_menu_anchor(index);
    auto* menu = new HeaderMenu(this, m_theme, m_dark, std::move(items));
    menu->open(*m_menu_anchor, HeaderMenu::Align::Left);
}

void LeftPane::open_row_menu(const PaneRow& row)
{
    const auto found = std::find_if(m_rows.begin(), m_rows.end(), [&row](const PaneRow& candidate) {
        return candidate.kind == row.kind && candidate.plate == row.plate && candidate.object == row.object &&
               candidate.copy == row.copy && candidate.volume == row.volume;
    });
    if (found == m_rows.end())
        return;
    const std::size_t index = static_cast<std::size_t>(found - m_rows.begin());
    switch (row.kind) {
    case PaneRow::Kind::Plate: open_menu(index, plate_menu(row)); break;
    case PaneRow::Kind::Object: open_menu(index, object_menu(row)); break;
    case PaneRow::Kind::Copy: open_menu(index, copy_menu(row)); break;
    case PaneRow::Kind::Volume: open_menu(index, volume_menu(row)); break;
    case PaneRow::Kind::OffPlateHeader: break;
    }
}

std::vector<HeaderMenuItem> LeftPane::plate_menu(const PaneRow& row)
{
    std::vector<HeaderMenuItem> items;
    for (const PlateAction action : m_workspace.plate_actions(row.plate)) {
        HeaderMenuItem item;
        switch (action) {
        case PlateAction::Rename: item.label = _L("Rename"); break;
        case PlateAction::Arrange: item.label = _L("Arrange"); break;
        case PlateAction::AutoOrient: item.label = _L("Auto-orient"); break;
        case PlateAction::ToggleLock: item.label = row.locked ? _L("Unlock") : _L("Lock"); break;
        case PlateAction::Settings: item.label = _L("Plate settings…"); break;
        case PlateAction::MoveToFront: item.label = _L("Move to front"); break;
        case PlateAction::FilamentGrouping: item.label = _L("Filament grouping"); break;
        case PlateAction::Delete:
            item.label             = _L("Delete plate");
            item.separator         = true;
            item.decoration.danger = true;
            break;
        }
        const PlateId plate = row.plate;
        item.invoke = [this, plate, action] { run_command(m_workspace.run_plate_action(plate, action), "plate action"); };
        items.push_back(std::move(item));
    }
    return items;
}

std::vector<HeaderMenuItem> LeftPane::object_menu(const PaneRow& row)
{
    std::vector<HeaderMenuItem> items;
    const ObjectId object = row.object;

    HeaderMenuItem rename;
    rename.label  = _L("Rename");
    rename.invoke = [this, object, name = row.name] {
        wxTextEntryDialog dialog(this, _L("Name"), _L("Rename object"), wxString::FromUTF8(name));
        if (dialog.ShowModal() != wxID_OK)
            return;
        run_command(m_workspace.rename_object(object, std::string(dialog.GetValue().ToUTF8())), "rename object");
    };
    items.push_back(std::move(rename));

    bool separated_tools = false;
    for (const ObjectAction action : m_workspace.object_actions(object)) {
        if (action == ObjectAction::SplitToParts)
            continue;
        HeaderMenuItem item;
        switch (action) {
        case ObjectAction::Quantity: item.label = _L("Set number of instances…"); break;
        case ObjectAction::TogglePrintable:
            item.label = _L("Printable");
            item.decoration.check = row.object_printable;
            break;
        case ObjectAction::Filament:
            item.label               = _L("Change filament");
            item.decoration.trailing = HeaderIcon::Right;
            item.invoke              = [this, object] { show_filament_choices(object); };
            items.push_back(std::move(item));
            continue;
        case ObjectAction::Repair: item.label = _L("Fix model"); break;
        case ObjectAction::Split:
            item.label = _L("Split");
            item.decoration.trailing = HeaderIcon::Right;
            item.invoke = [this, object] { show_split_choices(object); };
            if (!separated_tools) item.separator = true;
            items.push_back(std::move(item));
            continue;
        case ObjectAction::SplitToParts: break;
        }
        if (action == ObjectAction::Repair && !separated_tools) {
            item.separator = true;
            separated_tools = true;
        }
        item.invoke = [this, object, action] { run_command(m_workspace.run_object_action(object, action), "object action"); };
        items.push_back(std::move(item));
    }

    HeaderMenuItem remove;
    remove.label             = _L("Delete");
    remove.separator         = true;
    remove.decoration.danger = true;
    remove.invoke            = [this, object] { run_command(m_workspace.remove_object(object), "delete object"); };
    items.push_back(std::move(remove));
    return items;
}

std::vector<HeaderMenuItem> LeftPane::copy_menu(const PaneRow& row)
{
    std::vector<HeaderMenuItem> items;
    const InstanceId copy   = row.copy;
    const ObjectId   object = row.object;

    if (const auto actions = m_workspace.object_actions(object);
        std::find(actions.begin(), actions.end(), ObjectAction::Quantity) != actions.end()) {
        HeaderMenuItem quantity;
        quantity.label = _L("Set number of instances…");
        quantity.invoke = [this, object] { run_command(m_workspace.run_object_action(object, ObjectAction::Quantity), "set quantity"); };
        items.push_back(std::move(quantity));
    }

    HeaderMenuItem printable;
    printable.label = _L("Printable");
    printable.decoration.check = row.copy_printable;
    printable.invoke = [this, copy] { run_command(m_workspace.toggle_copy_printable(copy), "toggle copy"); };
    items.push_back(std::move(printable));

    if (const auto actions = m_workspace.object_actions(object);
        std::find(actions.begin(), actions.end(), ObjectAction::Filament) != actions.end()) {
        HeaderMenuItem filament;
        filament.label = _L("Change object filament");
        filament.decoration.trailing = HeaderIcon::Right;
        filament.invoke = [this, object] { show_filament_choices(object); };
        items.push_back(std::move(filament));
    }

    HeaderMenuItem remove;
    remove.label             = _L("Delete copy");
    remove.separator         = true;
    remove.decoration.danger = true;
    remove.invoke            = [this, copy, object] {
        // A copy's position in its object is what Orca deletes it by.
        for (const OutlineObject& candidate : m_outline.objects) {
            if (candidate.id != object)
                continue;
            for (std::size_t index = 0; index < candidate.copies.size(); ++index)
                if (candidate.copies[index].id == copy) {
                    DeleteItem item;
                    item.kind     = DeleteItem::Kind::Instance;
                    item.object   = object;
                    item.instance = index;
                    run_command(m_workspace.delete_items({item}), "delete copy");
                    return;
                }
        }
    };
    items.push_back(std::move(remove));
    return items;
}

std::vector<HeaderMenuItem> LeftPane::volume_menu(const PaneRow& row)
{
    std::vector<HeaderMenuItem> items;
    HeaderMenuItem remove;
    remove.label             = _L("Delete part");
    remove.decoration.danger = true;
    remove.invoke            = [this, object = row.object, volume = row.volume] {
        DeleteItem item;
        item.kind   = DeleteItem::Kind::Part;
        item.object = object;
        item.part   = volume.value();
        run_command(m_workspace.delete_items({item}), "delete part");
    };
    items.push_back(std::move(remove));
    return items;
}

void LeftPane::show_filament_choices(ObjectId object)
{
    // The popup that ran this row is closing; the choices open in a new one
    // on the next turn, anchored to the same row.
    CallAfter([this, object] {
        const auto row = std::find_if(m_rows.begin(), m_rows.end(), [object](const PaneRow& candidate) {
            return candidate.kind == PaneRow::Kind::Object && candidate.object == object;
        });
        if (row == m_rows.end())
            return;
        std::vector<HeaderMenuItem> items;
        for (const SetupCommands::FilamentSlot& slot : SetupCommands::filament_slots()) {
            HeaderMenuItem item;
            item.label = slot.filament.alias.empty() ? wxString::FromUTF8(slot.filament.preset_name) : slot.filament.alias;
            item.name = wxString::Format("%d  %s", int(slot.index + 1), item.label);
            item.decoration.lead  = wxString::Format("%d", int(slot.index + 1));
            item.decoration.dot   = wxColour(slot.colour);
            item.decoration.check = row->filament.value_or(1) == int(slot.index + 1);
            item.invoke = [this, object, index = slot.index] {
                run_command(m_workspace.run_object_action(object, ObjectAction::Filament, int(index + 1)), "set filament");
            };
            items.push_back(std::move(item));
        }
        open_menu(static_cast<std::size_t>(row - m_rows.begin()), std::move(items));
    });
}

void LeftPane::show_split_choices(ObjectId object)
{
    CallAfter([this, object] {
        const auto row = std::find_if(m_rows.begin(), m_rows.end(), [object](const PaneRow& candidate) {
            return candidate.kind == PaneRow::Kind::Object && candidate.object == object;
        });
        if (row == m_rows.end())
            return;
        std::vector<HeaderMenuItem> items;
        const auto actions = m_workspace.object_actions(object);
        const auto add = [this, object, &items](ObjectAction action, const wxString& label) {
            HeaderMenuItem item;
            item.label = label;
            item.invoke = [this, object, action] { run_command(m_workspace.run_object_action(object, action), "split object"); };
            items.push_back(std::move(item));
        };
        if (std::find(actions.begin(), actions.end(), ObjectAction::Split) != actions.end())
            add(ObjectAction::Split, _L("To objects"));
        if (std::find(actions.begin(), actions.end(), ObjectAction::SplitToParts) != actions.end())
            add(ObjectAction::SplitToParts, _L("To parts"));
        open_menu(static_cast<std::size_t>(row - m_rows.begin()), std::move(items));
    });
}

std::vector<std::size_t> LeftPane::action_hits() const
{
    std::vector<std::size_t> hits;
    for (std::size_t index = 0; index < m_hits.size(); ++index)
        if (m_hits[index].kind == Hit::Kind::Action)
            hits.push_back(index);
    return hits;
}

bool LeftPane::handle_key(int key)
{
    if (m_navigation.tab() == PaneTab::Project) {
        const std::vector<std::size_t> actions = action_hits();
        if (actions.empty())
            return false;
        switch (key) {
        case WXK_UP:
        case WXK_DOWN:
            m_tab_focus_visible = false;
            m_focus_visible = true;
            m_action_focus  = std::clamp(m_action_focus + (key == WXK_DOWN ? 1 : -1), 0, int(actions.size()) - 1);
            SetName(m_hits[actions[m_action_focus]].label);
            Refresh();
            return true;
        case WXK_RETURN:
        case WXK_NUMPAD_ENTER:
        case WXK_SPACE:
            if (m_action_focus >= 0 && m_action_focus < int(actions.size())) {
                const std::function<void()> invoke = m_hits[actions[m_action_focus]].invoke;
                invoke();
            }
            return true;
        default: return false;
        }
    }
    switch (key) {
    case WXK_UP: move_focus(-1); return true;
    case WXK_DOWN: move_focus(1); return true;
    case WXK_RETURN:
    case WXK_NUMPAD_ENTER:
    case WXK_SPACE:
        if (m_focus_row >= 0 && m_focus_row < static_cast<int>(m_rows.size()))
            activate_row(m_rows[m_focus_row]);
        return true;
    case WXK_RIGHT:
    case WXK_LEFT:
        if (m_focus_row >= 0 && m_focus_row < static_cast<int>(m_rows.size())) {
            const PaneRow& row = m_rows[m_focus_row];
            if (row.expandable && row.expanded == (key == WXK_LEFT))
                toggle_row(row);
        }
        return true;
    default: return false;
    }
}

void LeftPane::move_focus(int delta)
{
    if (m_rows.empty())
        return;
    m_focus_visible = true;
    int next = m_focus_row < 0 ? (delta > 0 ? 0 : static_cast<int>(m_rows.size()) - 1) : m_focus_row + delta;
    // Group headers take no focus.
    while (next >= 0 && next < static_cast<int>(m_rows.size()) && m_rows[next].kind == PaneRow::Kind::OffPlateHeader)
        next += delta;
    if (next < 0 || next >= static_cast<int>(m_rows.size()))
        return;
    m_focus_row = next;
    m_tab_focus_visible = false;
    const PaneRow& focused = m_rows[m_focus_row];
    wxString accessible_name = focused.kind == PaneRow::Kind::Copy
        ? wxString::Format(_L("Copy %d"), int(focused.ordinal)) : wxString::FromUTF8(focused.name);
    if (focused.customization.setting_overrides > 0)
        accessible_name += middle_dot() + wxString::Format(_L("%d per-object settings"), int(focused.customization.setting_overrides));
    SetName(accessible_name);
    activate_row(focused);
    Refresh();
}

// -- Pointer and keyboard ---------------------------------------------------

const LeftPane::Hit* LeftPane::hit_at(const wxPoint& point, std::initializer_list<Hit::Kind> kinds) const
{
    // The last control drawn is the one on top.
    for (auto hit = m_hits.rbegin(); hit != m_hits.rend(); ++hit)
        if (hit->rect.Contains(point) && std::find(kinds.begin(), kinds.end(), hit->kind) != kinds.end())
            return &*hit;
    return nullptr;
}

void LeftPane::on_left_down(wxMouseEvent& event)
{
    SetFocus();
    m_focus_visible = false;
    m_tab_focus_visible = false;
    const wxPoint point = event.GetPosition();
    if (const Hit* tab = hit_at(point, {Hit::Kind::Tab})) {
        if (tab->tab == PaneTab::Plates)
            show_plates();
        else
            show_project();
        return;
    }
    if (const Hit* action = hit_at(point, {Hit::Kind::Action})) {
        // The action may rebuild the record this hit lives in.
        const std::function<void()> invoke = action->invoke;
        invoke();
        return;
    }
    if (hit_at(point, {Hit::Kind::AddPlate})) {
        add_plate();
        return;
    }
    if (const Hit* mark = hit_at(point, {Hit::Kind::Mark})) {
        const PaneRow& row = m_rows[mark->row];
        if (row.kind == PaneRow::Kind::Plate)
            run_command(m_workspace.run_plate_action(row.plate, PlateAction::Settings), "plate settings");
        else
            open_menu(mark->row, customization_menu(row));
        return;
    }
    if (const Hit* overflow = hit_at(point, {Hit::Kind::Overflow})) {
        open_row_menu(m_rows[overflow->row]);
        return;
    }
    if (const Hit* expander = hit_at(point, {Hit::Kind::Expander})) {
        toggle_row(m_rows[expander->row]);
        return;
    }
    if (const Hit* row = hit_at(point, {Hit::Kind::Row})) {
        m_focus_row = static_cast<int>(row->row);
        activate_row(m_rows[row->row]);
    }
}

void LeftPane::on_right_down(wxMouseEvent& event)
{
    SetFocus();
    if (const Hit* row = hit_at(event.GetPosition(), {Hit::Kind::Row}))
        open_row_menu(m_rows[row->row]);
}

void LeftPane::on_motion(wxMouseEvent& event)
{
    const Hit* row = hit_at(event.GetPosition(), {Hit::Kind::Row});
    const int  now = row == nullptr ? -1 : static_cast<int>(row->row);
    if (now != m_hover_row) {
        m_hover_row = now;
        Refresh();
    }
}

void LeftPane::on_leave(wxMouseEvent&)
{
    if (m_hover_row != -1) {
        m_hover_row = -1;
        Refresh();
    }
}

void LeftPane::on_wheel(wxMouseEvent& event)
{
    const int lines = event.GetWheelRotation() / std::max(1, event.GetWheelDelta());
    m_scroll -= lines * FromDIP(m_theme.metrics().left_pane.object_row_height);
    clamp_scroll();
    Refresh();
}

void LeftPane::on_key_down(wxKeyEvent& event)
{
    if (event.ShiftDown() && event.GetKeyCode() == WXK_F10 &&
        m_navigation.tab() == PaneTab::Plates && m_focus_row >= 0 && m_focus_row < int(m_rows.size())) {
        open_row_menu(m_rows[m_focus_row]);
        return;
    }
    if (event.ControlDown() && event.GetKeyCode() == WXK_TAB) {
        if (m_navigation.tab() == PaneTab::Plates)
            show_project();
        else
            show_plates();
        m_tab_focus_visible = true;
        SetName(m_navigation.tab() == PaneTab::Plates ? _L("Plates tab") : _L("Project tab"));
        Refresh();
        return;
    }
    if (event.GetKeyCode() == WXK_ESCAPE && handle_escape())
        return;
    if (!handle_key(event.GetKeyCode()))
        event.Skip();
}

void LeftPane::clamp_scroll()
{
    const LeftPaneMetrics& pane = m_theme.metrics().left_pane;
    const int viewport = GetClientSize().y - 2 * FromDIP(2 * pane.padding_y + pane.tab_height);
    m_scroll = std::clamp(m_scroll, 0, std::max(0, m_content_height - viewport));
}

// -- Painting ---------------------------------------------------------------

wxRect LeftPane::rect_of(std::size_t row, Part part) const
{
    const Hit::Kind kind = part == Part::Row ? Hit::Kind::Row : part == Part::Expander ? Hit::Kind::Expander :
                           part == Part::Mark ? Hit::Kind::Mark : Hit::Kind::Overflow;
    for (const Hit& hit : m_hits)
        if (hit.kind == kind && hit.row == row)
            return hit.rect;
    return wxRect();
}

wxRect LeftPane::tab_rect(PaneTab tab) const
{
    for (const Hit& hit : m_hits)
        if (hit.kind == Hit::Kind::Tab && hit.tab == tab)
            return hit.rect;
    return wxRect();
}

wxRect LeftPane::add_plate_rect() const
{
    for (const Hit& hit : m_hits)
        if (hit.kind == Hit::Kind::AddPlate)
            return hit.rect;
    return wxRect();
}

std::vector<wxString> LeftPane::action_labels() const
{
    std::vector<wxString> labels;
    for (const Hit& hit : m_hits)
        if (hit.kind == Hit::Kind::Action)
            labels.push_back(hit.label);
    return labels;
}

wxRect LeftPane::action_rect(const wxString& label) const
{
    for (const Hit& hit : m_hits)
        if (hit.kind == Hit::Kind::Action && hit.label == label)
            return hit.rect;
    return wxRect();
}

bool LeftPane::press_action(const wxString& label)
{
    for (const Hit& hit : m_hits)
        if (hit.kind == Hit::Kind::Action && hit.label == label) {
            const std::function<void()> invoke = hit.invoke;
            invoke();
            return true;
        }
    return false;
}

wxBitmap LeftPane::snapshot()
{
    const wxSize size = GetClientSize();
    wxBitmap bitmap(size.x, size.y);
    {
        wxMemoryDC dc(bitmap);
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (gc)
            draw(*gc, size);
    }
    return bitmap;
}

void LeftPane::paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc)
        return;
    draw(*gc, GetClientSize());
}

void LeftPane::draw(wxGraphicsContext& gc, const wxSize& client)
{
    const ShellPalette&    p    = m_theme.palette(m_dark);
    const LeftPaneMetrics& pane = m_theme.metrics().left_pane;

    gc.SetPen(*wxTRANSPARENT_PEN);
    gc.SetBrush(wxBrush(p.surface_subtle));
    gc.DrawRectangle(0, 0, client.x, client.y);

    m_hits.clear();
    draw_tabs(gc, client.x);

    // The pane toggle owns the first header band. The tabs occupy the matching
    // row below it, leaving the toggle in a stable place as the pane opens and
    // closes while keeping Plates / Project visually separate.
    const int header_height = FromDIP(2 * pane.padding_y + pane.tab_height);
    const int top = 2 * header_height;
    gc.PushState();
    gc.Clip(0, top, client.x, std::max(0, client.y - top));
    m_content_height = (m_navigation.tab() == PaneTab::Plates ? draw_plates(gc, top - m_scroll, client.x)
                                                              : draw_project(gc, top - m_scroll, client.x)) + m_scroll - top;
    gc.PopState();
    clamp_scroll();

    if (m_navigation.tab() == PaneTab::Project && m_focus_visible && HasFocus()) {
        const std::vector<std::size_t> actions = action_hits();
        if (m_action_focus >= 0 && m_action_focus < int(actions.size())) {
            const wxRect rect = m_hits[actions[m_action_focus]].rect;
            gc.SetPen(wxPen(p.border_focus, FromDIP(m_theme.metrics().focus.width)));
            gc.SetBrush(*wxTRANSPARENT_BRUSH);
            gc.DrawRoundedRectangle(rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2, FromDIP(m_theme.metrics().radius_standard));
        }
    }
}

void LeftPane::draw_tabs(wxGraphicsContext& gc, int width)
{
    const ShellPalette&    p    = m_theme.palette(m_dark);
    const LeftPaneMetrics& pane = m_theme.metrics().left_pane;
    const int header_height = FromDIP(2 * pane.padding_y + pane.tab_height);
    const int top    = header_height + FromDIP(pane.padding_y);
    const int height = FromDIP(pane.tab_height);
    const int tabs_bottom = 2 * header_height;

    gc.SetPen(*wxTRANSPARENT_PEN);
    gc.SetBrush(wxBrush(p.surface_canvas));
    gc.DrawRectangle(0, 0, width, tabs_bottom);

    const struct { PaneTab tab; wxString label; } tabs[] = {{PaneTab::Plates, _L("Plates")}, {PaneTab::Project, _L("Project")}};
    for (int index = 0; index < 2; ++index) {
        const bool   active = m_navigation.tab() == tabs[index].tab;
        const wxRect rect(index * width / 2, top, width / 2, height);
        gc.SetFont(m_theme.font(active ? TextRole::LabelBold : TextRole::Label), active ? p.text_primary : p.text_secondary);
        double tw = 0, th = 0;
        gc.GetTextExtent(tabs[index].label, &tw, &th);
        gc.DrawText(tabs[index].label, rect.x + (rect.width - tw) / 2, rect.y + (rect.height - th) / 2);
        if (active) {
            gc.SetBrush(wxBrush(p.action_primary));
            gc.DrawRectangle(rect.x, rect.y + rect.height - FromDIP(pane.tab_indicator_height), rect.width,
                             FromDIP(pane.tab_indicator_height));
            if (HasFocus() && m_tab_focus_visible) {
                gc.SetPen(wxPen(p.border_focus, FromDIP(m_theme.metrics().focus.width)));
                gc.SetBrush(*wxTRANSPARENT_BRUSH);
                gc.DrawRoundedRectangle(rect.x, rect.y, rect.width, rect.height,
                                        FromDIP(m_theme.metrics().radius_standard));
            }
        }
        m_hits.push_back({Hit::Kind::Tab, rect, 0, tabs[index].tab});
    }
    gc.SetPen(wxPen(p.border_subtle));
    gc.StrokeLine(0, header_height - 0.5, width, header_height - 0.5);
    gc.StrokeLine(0, tabs_bottom - 0.5, width, tabs_bottom - 0.5);
}

int LeftPane::draw_plates(wxGraphicsContext& gc, int top, int width)
{
    const ShellPalette&    p    = m_theme.palette(m_dark);
    const LeftPaneMetrics& pane = m_theme.metrics().left_pane;
    int y = top + FromDIP(pane.list_padding_y);

    std::size_t index = 0;
    while (index < m_rows.size()) {
        const PaneRow& row = m_rows[index];
        // A block is a plate row with everything listed under it, or the
        // off-plate group.
        std::size_t end = index + 1;
        while (end < m_rows.size() && m_rows[end].kind != PaneRow::Kind::Plate && m_rows[end].kind != PaneRow::Kind::OffPlateHeader)
            ++end;

        int block = FromDIP(pane.plate_row_height);
        // A folded plate adds its summary line and the padding under it.
        if (row.kind == PaneRow::Kind::Plate && !row.expanded)
            block += FromDIP(m_theme.type_style(TextRole::Metadata).line_height + pane.summary_padding_bottom);
        block += static_cast<int>(end - index - 1) * FromDIP(pane.object_row_height);

        // The block's ground, then its rows.
        gc.SetPen(*wxTRANSPARENT_PEN);
        gc.SetBrush(wxBrush(p.surface_canvas));
        gc.DrawRoundedRectangle(0, y, width, block, FromDIP(m_theme.metrics().radius_standard));

        int row_y = y;
        draw_plate_row(gc, row, index, row_y, width, block);
        for (std::size_t child = index + 1; child < end; ++child)
            draw_object_row(gc, m_rows[child], child, row_y, width);
        y += block + FromDIP(pane.row_gap);
        index = end;
    }

    // Add plate.
    const int padding_x = FromDIP(pane.add_plate_padding_x);
    const wxRect button(padding_x, y + FromDIP(pane.add_plate_padding_top), width - 2 * padding_x,
                        FromDIP(pane.add_plate_button_height));
    gc.SetPen(wxPen(p.border_subtle, 1));
    gc.SetBrush(*wxTRANSPARENT_BRUSH);
    gc.DrawRoundedRectangle(button.x + 0.5, button.y + 0.5, button.width - 1, button.height - 1,
                            FromDIP(m_theme.metrics().radius_compact));
    const bool can_add = m_outline.can_add_plate;
    gc.SetFont(m_theme.font(TextRole::LabelBold), can_add ? p.text_primary : p.action_disabled_text);
    const wxString label = wxString::FromUTF8("+ ") + _L("Add plate");
    double tw = 0, th = 0;
    gc.GetTextExtent(label, &tw, &th);
    gc.DrawText(label, button.x + (button.width - tw) / 2, button.y + (button.height - th) / 2);
    if (can_add)
        m_hits.push_back({Hit::Kind::AddPlate, button, 0, PaneTab::Plates});

    return y + FromDIP(pane.add_plate_padding_top + pane.add_plate_button_height + pane.add_plate_padding_bottom);
}

void LeftPane::draw_focus_ring(wxGraphicsContext& gc, std::size_t index, const wxRect& rect)
{
    if (!HasFocus() || !m_focus_visible || static_cast<int>(index) != m_focus_row)
        return;
    gc.SetPen(wxPen(m_theme.palette(m_dark).border_focus, FromDIP(m_theme.metrics().focus.width)));
    gc.SetBrush(*wxTRANSPARENT_BRUSH);
    const int inset = FromDIP(1);
    gc.DrawRoundedRectangle(rect.x + inset, rect.y + inset, rect.width - 2 * inset, rect.height - 2 * inset,
                            FromDIP(m_theme.metrics().radius_standard));
}

void LeftPane::draw_plate_row(wxGraphicsContext& gc, const PaneRow& row, std::size_t index, int& y, int width, int block_height)
{
    const ShellPalette&    p    = m_theme.palette(m_dark);
    const LeftPaneMetrics& pane = m_theme.metrics().left_pane;
    const int line   = FromDIP(pane.plate_row_height);
    const int glyph  = FromDIP(pane.glyph_size);
    const int pad_x  = FromDIP(pane.row_padding_x);
    const int gap    = FromDIP(pane.plate_gap);
    const wxRect row_rect(0, y, width, row.expanded ? line : block_height);
    const bool   hot = static_cast<int>(index) == m_hover_row;

    if (row.kind == PaneRow::Kind::OffPlateHeader) {
        gc.SetFont(m_theme.font(TextRole::LabelBold), p.text_primary);
        const wxString text = _L("Not on a plate") + middle_dot() + wxString::Format("%d", int(row.copies));
        double tw = 0, th = 0;
        gc.GetTextExtent(text, &tw, &th);
        gc.DrawText(fit(gc, text, width - 2 * pad_x), pad_x, y + (line - th) / 2);
        y += line;
        return;
    }

    if (hot) {
        gc.SetPen(*wxTRANSPARENT_PEN);
        gc.SetBrush(wxBrush(p.surface_selected));
        gc.DrawRoundedRectangle(0, y, width, row_rect.height, FromDIP(m_theme.metrics().radius_standard));
    }
    m_hits.push_back({Hit::Kind::Row, row_rect, index, PaneTab::Plates});
    draw_focus_ring(gc, index, row_rect);

    // Disclosure: open on the active plate, closed on the others. Plates do not
    // fold on their own account -- opening one means making it the active plate --
    // so the glyph is a marker, not a control.
    draw_header_icon(gc, row.expanded ? HeaderIcon::Down : HeaderIcon::Right, pad_x, y + (line - glyph) / 2, glyph, p.text_secondary);

    // Right edge: overflow on the active plate or under the pointer, then the
    // status word. Marks sit between the name and the status.
    int right = width - pad_x;
    const bool show_overflow = row.expanded || hot;
    if (show_overflow) {
        const wxRect overflow(right - glyph, y + (line - glyph) / 2, glyph, glyph);
        draw_header_icon(gc, HeaderIcon::More, overflow.x, overflow.y, glyph, p.text_secondary);
        m_hits.push_back({Hit::Kind::Overflow, overflow, index, PaneTab::Plates});
        right = overflow.x - gap;
    }
    gc.SetFont(m_theme.font(TextRole::Metadata), p.text_secondary);
    const wxString status = plate_status(row);
    const double status_width = text_width(gc, status);
    double th = 0, unused = 0;
    gc.GetTextExtent(status, &unused, &th);
    gc.DrawText(status, right - status_width, y + (line - th) / 2);
    right -= static_cast<int>(status_width) + gap;

    if (row.custom_settings)
        right -= draw_customization_mark(gc, index, right, y, line) + gap;
    if (row.locked) {
        right -= glyph;
        draw_header_icon(gc, HeaderIcon::Lock, right, y + (line - glyph) / 2, glyph, p.text_secondary);
        right -= gap;
    }

    // Name.
    const int name_x = pad_x + glyph + gap;
    gc.SetFont(m_theme.font(TextRole::LabelBold), p.text_primary);
    const wxString name = fit(gc, wxString::FromUTF8(row.name), std::max(0, right - name_x));
    gc.GetTextExtent(name, &unused, &th);
    gc.DrawText(name, name_x, y + (line - th) / 2);

    y += line;
    if (!row.expanded) {
        gc.SetFont(m_theme.font(TextRole::Metadata), p.text_secondary);
        const wxString summary = fit(gc, plate_summary(row), width - FromDIP(pane.summary_indent) - pad_x);
        gc.DrawText(summary, FromDIP(pane.summary_indent), y);
        y += block_height - line;
    }
}

void LeftPane::draw_object_row(wxGraphicsContext& gc, const PaneRow& row, std::size_t index, int& y, int width)
{
    const ShellPalette&    p    = m_theme.palette(m_dark);
    const LeftPaneMetrics& pane = m_theme.metrics().left_pane;
    const int line  = FromDIP(pane.object_row_height);
    const int glyph = FromDIP(pane.glyph_size);
    const int pad_x = FromDIP(pane.row_padding_x);
    const int gap   = FromDIP(pane.object_gap);
    const bool hot  = static_cast<int>(index) == m_hover_row;
    const int depth_indent = std::max(0, row.depth - 1) * FromDIP(pane.glyph_size);
    const int left  = FromDIP(pane.object_indent) + depth_indent;

    if (row.selected || hot) {
        gc.SetPen(*wxTRANSPARENT_PEN);
        gc.SetBrush(wxBrush(p.surface_selected));
        gc.DrawRoundedRectangle(0, y, width, line, FromDIP(m_theme.metrics().radius_standard));
    }
    m_hits.push_back({Hit::Kind::Row, wxRect(0, y, width, line), index, PaneTab::Plates});

    draw_focus_ring(gc, index, wxRect(0, y, width, line));

    if (row.expandable) {
        draw_header_icon(gc, row.expanded ? HeaderIcon::Down : HeaderIcon::Right, left - glyph - gap + gap,
                         y + (line - glyph) / 2, glyph, p.text_secondary);
        m_hits.push_back({Hit::Kind::Expander, wxRect(left - glyph, y, glyph + gap, line), index, PaneTab::Plates});
    }

    int right = width - pad_x;
    double th = 0, unused = 0;
    if (row.selected || hot) {
        const wxRect overflow(right - glyph, y + (line - glyph) / 2, glyph, glyph);
        draw_header_icon(gc, HeaderIcon::More, overflow.x, overflow.y, glyph, p.text_secondary);
        m_hits.push_back({Hit::Kind::Overflow, overflow, index, PaneTab::Plates});
        right = overflow.x - gap;
    }

    gc.SetFont(m_theme.font(TextRole::Metadata), p.text_secondary);
    const auto put_right = [&](const wxString& text, const wxColour& colour) {
        gc.SetFont(m_theme.font(TextRole::Metadata), colour);
        right -= static_cast<int>(text_width(gc, text));
        gc.GetTextExtent(text, &unused, &th);
        gc.DrawText(text, right, y + (line - th) / 2);
        right -= gap;
    };

    wxString name;
    switch (row.kind) {
    case PaneRow::Kind::Object: {
        // Right to left, the reverse of the frame's order: mesh error beside the
        // overflow, then won't-print, the customization mark, the filament, the copies.
        if (row.mesh.any())
            put_right(row.mesh.open_edges > 0 ? wxString::Format(_L("%d mesh errors"), int(row.mesh.open_edges)) : _L("Mesh repaired"),
                      row.mesh.open_edges > 0 ? p.status_danger : p.status_warning);
        if (row.all_wont_print())
            put_right(_L("won't print"), p.text_secondary);
        else if (row.wont_print > 0)
            put_right(wxString::Format(_L("%d won't print"), int(row.wont_print)), p.text_secondary);
        if (row.customized)
            right -= draw_customization_mark(gc, index, right, y, line) + gap;
        if (row.filament) {
            put_right(wxString::Format("%d", *row.filament), p.text_secondary);
            // The slot's colour beside its number; no colour, no swatch.
            const wxColour swatch(wxString::FromUTF8(row.filament_colour));
            if (!row.filament_colour.empty() && swatch.IsOk()) {
                const int dot = FromDIP(m_theme.metrics().slot_dot.size);
                right -= dot;
                gc.SetPen(wxPen(p.border_subtle, 1));
                gc.SetBrush(wxBrush(swatch));
                gc.DrawEllipse(right, y + (line - dot) / 2.0, dot, dot);
                right -= gap;
            }
        }
        if (row.copies > 1)
            put_right(times() + wxString::Format("%d", int(row.copies)), p.text_secondary);
        name = wxString::FromUTF8(row.name);
        break;
    }
    case PaneRow::Kind::Copy:
        if (row.copy_wont_print)
            put_right(_L("won't print"), p.text_secondary);
        name = wxString::Format(_L("Copy %d"), int(row.ordinal));
        break;
    case PaneRow::Kind::Volume:
        if (row.mesh.any())
            put_right(row.mesh.open_edges > 0 ? wxString::Format(_L("%d mesh errors"), int(row.mesh.open_edges)) : _L("Mesh repaired"),
                      row.mesh.open_edges > 0 ? p.status_danger : p.status_warning);
        if (row.filament) {
            put_right(wxString::Format("%d", *row.filament), p.text_secondary);
            const wxColour swatch(wxString::FromUTF8(row.filament_colour));
            if (!row.filament_colour.empty() && swatch.IsOk()) {
                const int dot = FromDIP(m_theme.metrics().slot_dot.size);
                right -= dot;
                gc.SetPen(wxPen(p.border_subtle, 1));
                gc.SetBrush(wxBrush(swatch));
                gc.DrawEllipse(right, y + (line - dot) / 2.0, dot, dot);
                right -= gap;
            }
        }
        switch (row.role) {
        case VolumeRole::Part: put_right(_L("part"), p.text_secondary); break;
        case VolumeRole::Modifier: put_right(_L("modifier"), p.text_secondary); break;
        case VolumeRole::NegativePart: put_right(_L("negative part"), p.text_secondary); break;
        case VolumeRole::SupportEnforcer: put_right(_L("support enforcer"), p.text_secondary); break;
        case VolumeRole::SupportBlocker: put_right(_L("support blocker"), p.text_secondary); break;
        }
        name = wxString::FromUTF8(row.name);
        break;
    default: break;
    }

    gc.SetFont(m_theme.font(TextRole::Label), p.text_primary);
    name = fit(gc, name, std::max(0, right - left));
    gc.GetTextExtent(name, &unused, &th);
    gc.DrawText(name, left, y + (line - th) / 2);
    y += line;
}

} // namespace Slic3r::GUI::JusPrin
