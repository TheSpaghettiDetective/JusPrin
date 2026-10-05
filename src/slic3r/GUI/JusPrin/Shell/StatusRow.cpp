#include "StatusRow.hpp"
#include "HeaderControls.hpp"
#include "FilamentChipModel.hpp"
#include "FilamentMenu.hpp"
#include "PrinterMenu.hpp"
#include "PrinterFilamentChip.hpp"
#include "SetupCommands.hpp"
#include "ShellController.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"
#include "slic3r/GUI/JusPrin/Workspace/ProjectAutosave.hpp"
#include "slic3r/GUI/Event.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_Preview.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Notebook.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/ParamsDialog.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/GUI/Plater.hpp"
#include <wx/sizer.h>
#include <wx/msgdlg.h>
#include <wx/filedlg.h>
#include <wx/weakref.h>
#include <wx/dcbuffer.h>
#include <wx/dialog.h>
#include <wx/timer.h>
#include <algorithm>
#include <boost/log/trivial.hpp>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin {

namespace {

// Joining a wxString to a narrow literal decodes that literal through the
// locale's encoding -- UTF-8 on macOS, the ANSI code page on Windows, where
// these separators come out as the two or three characters their UTF-8 bytes
// happen to spell. Naming them once, decoded explicitly, keeps a separator
// from being written the unsafe way again.
const wxString& middle_dot() { static const wxString s = wxString::FromUTF8(" \xC2\xB7 "); return s; }
const wxString& em_dash()    { static const wxString s = wxString::FromUTF8(" \xE2\x80\x94 "); return s; }

// The header row is taller than any component recipe; no token names it. It
// is not a spacing step either, so it is written once here.
constexpr int kHeaderHeightDip = 56;

void show_project_error(wxWindow* parent, const wxString& title, const wxString& message, const std::string& detail)
{
    BOOST_LOG_TRIVIAL(error) << std::string(title.ToUTF8()) << ": " << detail;
    wxMessageBox(message, title, wxOK | wxICON_ERROR, parent);
}

class CheckPrintDialog final : public wxDialog
{
public:
    CheckPrintDialog(wxWindow* parent, Plater& plater, std::function<bool()> is_current)
        : wxDialog(parent, wxID_ANY, _L("Check Print"), wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
        , m_plater(plater)
        , m_is_current(std::move(is_current))
    {
        SetName(_L("Check Print"));
        m_preview = m_plater.create_preview(this);
        if (m_preview->get_canvas3d() == nullptr)
            throw std::runtime_error("Check Print could not create the Orca Preview canvas");
        auto* layout = new wxBoxSizer(wxVERTICAL);
        layout->Add(m_preview, 1, wxEXPAND);
        SetSizer(layout);
        SetClientSize(m_plater.GetClientSize());
        CentreOnParent();

        m_preview->get_canvas3d()->bind_event_handlers();
        m_subscription = m_plater.subscribe_project_state([this](const ProjectStateChanged&) { close_if_stale(); });
        m_plater.Bind(EVT_SLICE_STATUS_CHANGED, &CheckPrintDialog::on_slice_status_changed, this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { close_if_stale(); }, m_stale_timer.GetId());
        m_stale_timer.Start(200);
        Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
            if (event.GetKeyCode() == WXK_ESCAPE) EndModal(wxID_CANCEL);
            else event.Skip();
        });
    }

    ~CheckPrintDialog() override
    {
        m_stale_timer.Stop();
        m_plater.Unbind(EVT_SLICE_STATUS_CHANGED, &CheckPrintDialog::on_slice_status_changed, this);
        m_subscription.reset();
        if (m_preview->get_canvas3d())
            m_preview->get_canvas3d()->unbind_event_handlers();
    }

private:
    void close_if_stale()
    {
        if (IsModal() && !m_is_current())
            EndModal(wxID_CANCEL);
    }

    void on_slice_status_changed(wxCommandEvent& event)
    {
        close_if_stale();
        event.Skip();
    }

    Plater& m_plater;
    std::function<bool()> m_is_current;
    Preview* m_preview{nullptr};
    ProjectStateSubscription m_subscription;
    wxTimer m_stale_timer{this};
};

} // namespace

StatusRow::StatusRow(wxWindow*                  parent,
                     const ShellTheme&          theme,
                     Plater&                    plater,
                     Notebook&                  tabpanel,
                     Agent::ProjectPersistence& persistence)
    : wxPanel(parent, wxID_ANY)
    , m_theme(theme)
    , m_plater(plater)
    , m_tabpanel(tabpanel)
    , m_persistence(persistence)
{
    SetName("Project header");
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(-1, FromDIP(kHeaderHeightDip)));
    m_left_pane_toggle = new HeaderButton(this, theme, HeaderStyle::Outline, wxEmptyString, HeaderIcon::PanelLeftClosed);
    m_left_pane_toggle->SetName(_L("Plates and Project panel"));
    m_left_pane_toggle->SetToolTip(_L("Show the Plates and Project panel"));
    m_left_pane_toggle->Hide();
    m_left_pane_toggle->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (m_left_pane_toggle_callback)
            m_left_pane_toggle_callback();
    });
    m_home_button = new HeaderButton(this, theme, HeaderStyle::Quiet, _L("Home"), HeaderIcon::Back);
    m_home_button->SetName("Home navigation");
    m_chip = new PrinterFilamentChip(this, theme);
    m_slice_button = new HeaderButton(this, theme, HeaderStyle::PrimaryLeft, _L("Slice"), HeaderIcon::Slice);
    m_slice_button->SetName("Next print action");
    m_menu_button = new HeaderButton(this, theme, HeaderStyle::PrimaryRight, wxEmptyString, HeaderIcon::Down);
    m_menu_button->SetName(_L("Print actions"));
    m_overflow_button = new HeaderButton(this, theme, HeaderStyle::Outline, wxEmptyString, HeaderIcon::More);
    m_overflow_button->SetName("Project actions");
    m_overflow_button->SetToolTip(_L("Project details and preferences"));
    m_agent_toggle = new HeaderButton(this, theme, HeaderStyle::Outline, wxEmptyString, HeaderIcon::PanelOpen);
    m_agent_toggle->SetName("Agent panel");
    m_agent_toggle->SetToolTip(_L("Hide the Agent panel"));
    m_agent_toggle->Hide();
    m_agent_toggle->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { toggle_agent_pane(); });
    m_home_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { request_home(); });
    m_chip->on_printer_activated([this] { open_printer_menu(); });
    m_chip->on_filament_activated([this] { open_filament_menu(); });
    m_overflow_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { show_overflow_menu(); });
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { layout_header(); e.Skip(); });
    Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        const auto& p = m_theme.palette(m_dark);
        dc.SetBackground(wxBrush(p.surface_canvas)); dc.Clear();
        dc.SetPen(wxPen(p.border_subtle));
        dc.DrawLine(0, GetClientSize().y-1, GetClientSize().x, GetClientSize().y-1);
    });

    m_slice_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        request_action(primary_print_action(action_state()).primary.action);
    });
    m_menu_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (action_state().slicing) request_action(PrintAction::Cancel);
        else show_action_menu();
    });

    Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& event) {
        apply_appearance(GUI_App::dark_mode());
        event.Skip();
    });

    m_project_state_subscription = m_plater.subscribe_project_state(
        [this](const ProjectStateChanged&) { refresh(); });
    m_plater.Bind(EVT_SLICE_STATUS_CHANGED, &StatusRow::on_slice_status_changed, this);
    // The tab panel and this row are siblings, so wx may destroy either one
    // first at shutdown; only unbind while the tab panel still exists.
    m_tabpanel.Bind(wxEVT_DESTROY, &StatusRow::on_tabpanel_destroyed, this);
}

StatusRow::~StatusRow()
{
    if (m_tabpanel_alive) {
        m_plater.Unbind(EVT_SLICE_STATUS_CHANGED, &StatusRow::on_slice_status_changed, this);
        m_tabpanel.Unbind(wxEVT_DESTROY, &StatusRow::on_tabpanel_destroyed, this);
    }
}

void StatusRow::on_slice_status_changed(wxCommandEvent& event)
{
    refresh();
    event.Skip();
}

void StatusRow::on_tabpanel_destroyed(wxWindowDestroyEvent& event)
{
    if (event.GetWindow() == &m_tabpanel)
        m_tabpanel_alive = false;
    event.Skip();
}

void StatusRow::apply_appearance(bool dark)
{
    m_dark = dark;
    const ShellPalette& palette = m_theme.palette(dark);
    SetBackgroundColour(palette.surface_canvas);
    for (auto* button : {m_left_pane_toggle,m_home_button,m_slice_button,m_menu_button,m_overflow_button,m_agent_toggle})
        button->set_dark(dark);
    m_chip->set_dark(dark);
    Refresh();
}

void StatusRow::refresh()
{
    refresh_chip();
    m_overflow_button->SetToolTip(project_summary());

    const auto state = action_state();
    const auto actions = primary_print_action(state);
    m_slice_button->SetLabel(action_label(actions.primary.action, true));
    m_slice_button->Enable(actions.primary.enabled);
    m_slice_button->set_icon(state.slicing ? HeaderIcon::Slicing :
                             actions.primary.action == PrintAction::Print ? HeaderIcon::Print : HeaderIcon::Slice);
    m_slice_button->set_status(false, false);
    m_menu_button->set_icon(state.slicing ? HeaderIcon::Cancel : HeaderIcon::Down);
    m_menu_button->SetToolTip(state.slicing ? _L("Cancel slicing") : _L("Print actions"));
    m_menu_button->SetName(state.slicing ? _L("Cancel slicing") : _L("Print actions"));
    // An empty menu has no half to show; the primary keeps its full silhouette.
    m_menu_button->Show(!actions.menu.empty());
    m_slice_button->set_attached(!actions.menu.empty());
    layout_header();
}

PrintActionState StatusRow::action_state() const
{
    PrintActionState state;
    auto& plates = m_plater.get_partplate_list();
    auto* plate = plates.get_curr_plate();
    state.slicing = m_plater.is_background_process_slicing();
    state.plate_number = plates.get_curr_plate_index() + 1;
    state.plate_count = plates.get_plate_count();
    const bool editable = !m_plater.only_gcode_mode() && !m_plater.using_exported_file();
    for (int i = 0; i < state.plate_count; ++i) {
        auto* item = plates.get_plate(i);
        if (item->is_slice_result_valid()) ++state.sliced_count;
        if (editable && item->can_slice() && !item->is_slice_result_valid()) state.can_slice_all = true;
    }
    if (plate) {
        state.sliced = plate->is_slice_result_valid();
        state.can_slice = editable && plate->can_slice();
        state.can_print = plate->is_slice_result_ready_for_print();
        state.can_export = plate->is_slice_result_ready_for_export();
    }
    auto* presets = wxGetApp().preset_bundle;
    // Match the native print-all backend support. Other legacy hosts accept
    // only one plate, even when several valid slices exist.
    bool supports_all = presets && presets->use_bbl_network();
    if (presets && !supports_all) {
        const auto& config = presets->printers.get_edited_preset().config;
        const auto* host = config.option<ConfigOptionEnum<PrintHostType>>("host_type");
        supports_all = host && host->value == htSimplyPrint;
    }
    state.can_print_all = supports_all && plates.is_all_slice_results_ready_for_print();
    return state;
}

wxString StatusRow::action_label(PrintAction action, bool primary) const
{
    const auto state = action_state();
    switch (action) {
    case PrintAction::Slice: return _L("Slice");
    case PrintAction::SliceAll: return _L("Slice all plates");
    case PrintAction::Cancel: return primary ? _L("Slicing…") : _L("Cancel");
    case PrintAction::CheckPrint: return _L("Check Print");
    case PrintAction::Print: return primary ? (state.plate_count > 1 ? wxString::Format(_L("Print plate %d"), state.plate_number) : _L("Print")) : _L("Print…");
    case PrintAction::PrintAll: return _L("Print all plates…");
    case PrintAction::Export: return _L("Export sliced file…");
    }
    throw std::logic_error("Unknown print action");
}

std::tuple<std::uint64_t, std::uint64_t, std::uint64_t> StatusRow::print_target_identity() const
{
    auto* plate = m_plater.get_partplate_list().get_curr_plate();
    return {m_plater.project_state_session(), plate ? plate->id().id : 0,
            plate && plate->is_slice_result_valid() && !m_plater.is_background_process_slicing() && plate->get_slice_result() ?
                plate->get_slice_result()->id : 0};
}

void StatusRow::show_action_menu()
{
    const auto identity = print_target_identity();
    const auto state = action_state();
    const auto actions = primary_print_action(state);
    std::vector<HeaderMenuItem> menu;
    for (const auto& item : actions.menu) {
        HeaderIcon icon = HeaderIcon::Slice;
        if (item.action == PrintAction::CheckPrint) icon = HeaderIcon::Eye;
        else if (item.action == PrintAction::Print) icon = HeaderIcon::Print;
        else if (item.action == PrintAction::PrintAll || item.action == PrintAction::SliceAll) icon = HeaderIcon::Plates;
        else if (item.action == PrintAction::Export) icon = HeaderIcon::Export;
        else if (item.action == PrintAction::Cancel) icon = HeaderIcon::Cancel;
        wxString detail;
        if (item.action == PrintAction::PrintAll)
            detail = wxString::Format(_L("%d sliced"), state.sliced_count);
        menu.push_back({action_label(item.action), icon, detail, item.enabled, item.action == PrintAction::Export,
            [this,identity,action=item.action] {
                // The menu describes one plate/result. A later plate switch or
                // project replacement must not retarget that displayed command.
                if (print_target_identity() == identity) request_action(action);
            }});
    }
    if (!menu.empty()) (new HeaderMenu(this,m_theme,m_dark,std::move(menu)))->open(*m_menu_button);
}

void StatusRow::set_agent_pane_toggle(std::function<void()> toggle)
{
    m_agent_pane_toggle = std::move(toggle);
    m_agent_toggle->Show(static_cast<bool>(m_agent_pane_toggle));
    layout_header();
}

void StatusRow::set_agent_pane_collapsed(bool collapsed)
{
    m_agent_toggle->set_icon(collapsed ? HeaderIcon::PanelClosed : HeaderIcon::PanelOpen);
    m_agent_toggle->SetToolTip(collapsed ? _L("Show the Agent panel") : _L("Hide the Agent panel"));
    // The open pane carries its own copy at the same right edge. Keeping two
    // controls avoids reparenting a live native button as the pane changes.
    m_agent_toggle->Show(collapsed);
    layout_header();
}

void StatusRow::set_left_pane_toggle(std::function<void()> toggle)
{
    m_left_pane_toggle_callback = std::move(toggle);
    m_left_pane_toggle->Show(m_left_pane_available && m_left_pane_collapsed &&
                             static_cast<bool>(m_left_pane_toggle_callback));
    layout_header();
}

void StatusRow::set_left_pane_available(bool available)
{
    m_left_pane_available = available;
    m_left_pane_toggle->Show(available && m_left_pane_collapsed &&
                             static_cast<bool>(m_left_pane_toggle_callback));
    layout_header();
}

void StatusRow::set_left_pane_collapsed(bool collapsed)
{
    m_left_pane_collapsed = collapsed;
    m_left_pane_toggle->set_icon(collapsed ? HeaderIcon::PanelLeftClosed : HeaderIcon::PanelLeftOpen);
    m_left_pane_toggle->SetToolTip(collapsed ? _L("Show the Plates and Project panel") :
                                              _L("Hide the Plates and Project panel"));
    m_left_pane_toggle->Show(m_left_pane_available && collapsed &&
                             static_cast<bool>(m_left_pane_toggle_callback));
    layout_header();
}

void StatusRow::toggle_agent_pane()
{
    if (m_agent_pane_toggle)
        m_agent_pane_toggle();
}

void StatusRow::layout_header()
{
    const int margin = FromDIP(16), gap = FromDIP(8), height = GetClientSize().y;
    auto place = [height](HeaderButton* button, int x, int width = -1) {
        wxSize size = button->GetBestSize();
        if (width >= 0) size.x = width;
        button->SetSize(x,(height-size.y)/2,size.x,size.y);
    };
    int right = GetClientSize().x-margin;
    if (m_agent_toggle->IsShown()) {
        right -= m_agent_toggle->GetBestSize().x;
        place(m_agent_toggle,right);
        right -= gap;
    }
    right -= m_overflow_button->GetBestSize().x;
    place(m_overflow_button,right);
    right -= gap;
    if (m_menu_button->IsShown()) {
        right -= m_menu_button->GetBestSize().x;
        place(m_menu_button,right);
    }
    right -= m_slice_button->GetBestSize().x;
    place(m_slice_button,right);
    int home_x = margin;
    if (m_left_pane_toggle->IsShown()) {
        place(m_left_pane_toggle, home_x);
        home_x += m_left_pane_toggle->GetBestSize().x + gap;
    }
    place(m_home_button,home_x);
    const int left = home_x+m_home_button->GetBestSize().x+gap;
    const int available = std::max(0,right-gap-left);
    const int width = std::min(m_chip->GetBestSize().x,available);
    const wxSize chip = m_chip->GetBestSize();
    m_chip->SetSize(left+(available-width)/2,(height-chip.y)/2,width,chip.y);
}

wxString StatusRow::project_summary() const
{
    wxString name = m_autosave != nullptr ? wxString::FromUTF8(m_autosave->project_name())
                                          : m_plater.get_project_name();
    if (name.empty()) name = _L("Untitled");
    if (m_autosave != nullptr) {
        switch (m_autosave->state()) {
        case Workspace::ProjectAutosave::State::Saving: name += em_dash() + _L("Saving…"); break;
        case Workspace::ProjectAutosave::State::Saved: name += em_dash() + _L("Saved"); break;
        case Workspace::ProjectAutosave::State::Failed: name += em_dash() + _L("Couldn't save"); break;
        }
    } else if (m_plater.is_project_dirty()) {
        name += em_dash() + _L("Unsaved changes");
    }
    return name;
}

void StatusRow::request_home()
{
    m_tabpanel.SetSelection(MainFrame::tpHome);
}

// The chip renders Orca's own selection and nothing else: the printer, and
// one dot per project slot. Nothing here writes; a change goes through the
// filament menu and SetupCommands.
void StatusRow::refresh_chip()
{
    const auto printer = SetupCommands::current_printer();
    // One nozzle size is said once; a printer with several says each.
    std::vector<double> sizes = printer.nozzles;
    std::sort(sizes.begin(), sizes.end());
    sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
    wxString nozzle;
    for (double size : sizes)
        nozzle += (nozzle.empty() ? wxString() : wxString(" / ")) + wxString::Format("%g", size);
    wxString printer_tooltip = printer.nickname;
    if (!nozzle.empty())
        printer_tooltip += middle_dot() + nozzle + " mm " + (sizes.size() > 1 ? _L("nozzles") : _L("nozzle"));
    m_chip->set_printer(printer.nickname, printer.valid ? nozzle : wxString{}, printer_tooltip);

    const auto slots = SetupCommands::filament_slots();
    std::vector<ChipSlot> chip_slots;
    for (const auto& slot : slots)
        chip_slots.push_back({slot.filament.preset_name, std::string(slot.filament.alias.ToUTF8()),
                              std::string(slot.colour.ToUTF8()), slot.used});
    const FilamentChipModel model = describe_filament_chip(chip_slots);
    const wxString label = !model.name.empty() ? wxString::FromUTF8(model.name)
                                               : wxString::Format(wxPLURAL("%d filament", "%d filaments", int(model.filaments)),
                                                                  int(model.filaments));
    // Hover names every slot uncut. The swatches the design draws beside each
    // name are the dots on the chip itself; a tooltip carries text only.
    wxString tooltip;
    if (slots.size() == 1) {
        tooltip = slots.front().filament.alias;
    } else {
        const auto used = std::count_if(slots.begin(), slots.end(), [](const auto& slot) { return slot.used; });
        tooltip = used > 0 ? wxString::Format(_L("%d of %d slots on this plate"), int(used), int(slots.size()))
                           : wxString::Format(_L("%d slots"), int(slots.size()));
        for (const auto& slot : slots) {
            tooltip += wxString::Format("\n%d  %s", int(slot.index + 1), slot.filament.alias);
            if (slot.nozzle > 0.)
                tooltip += middle_dot() + wxString::Format("%g", slot.nozzle);
            if (used > 0 && !slot.used)
                tooltip += middle_dot() + _L("not on this plate");
        }
    }
    m_chip->set_filaments(model, label, tooltip);
}

void StatusRow::open_printer_menu()
{
    PrinterMenu::open(this, m_theme, m_dark, m_plater, m_chip->printer_half());
}

void StatusRow::open_filament_menu()
{
    FilamentMenu::Host host;
    host.plater      = &m_plater;
    host.changed     = [self = wxWeakRef<StatusRow>(this)] { if (self) self->refresh(); };
    host.visit_ended = [self = wxWeakRef<StatusRow>(this)](const FilamentMenu::Visit& visit) {
        if (self) self->on_filament_visit(visit);
    };
    FilamentMenu::open(this, m_theme, m_dark, std::move(host), m_chip->filament_half());
}

// One grey line per menu visit that changed a filament, never one per click,
// and none for a colour-only visit: the plan did not change. The chip and the
// pinned card already follow Orca's own settings-changed notification; the
// line reports, and never offers to undo, because the selection belongs to
// the chip.
void StatusRow::on_filament_visit(const FilamentMenu::Visit& visit)
{
    refresh();
    if (!m_note_sink || visit.filaments.empty()) return;
    const auto slots  = SetupCommands::filament_slots();
    const bool single = slots.size() == 1;
    auto name_of = [&](std::size_t slot) {
        return slot < slots.size() ? slots[slot].filament.alias : wxString::FromUTF8(visit.filaments.at(slot));
    };
    wxString note;
    wxString colour;
    if (visit.filaments.size() == 1) {
        const std::size_t slot = visit.filaments.begin()->first;
        note = single ? wxString::Format(_L("Filament is now %s."), name_of(slot))
                      : wxString::Format(_L("Slot %d is now %s."), int(slot + 1), name_of(slot));
        if (slot < slots.size()) {
            colour = slots[slot].colour;
            if (!slots[slot].filament.material.empty())
                note += " " + wxString::Format(_L("Heat and cooling follow %s; walls and infill unchanged."),
                                               slots[slot].filament.material);
        }
    } else {
        wxString changed;
        for (const auto& [slot, preset] : visit.filaments)
            changed += (changed.empty() ? wxString() : wxString(", ")) +
                       wxString::Format(_L("slot %d %s"), int(slot + 1), name_of(slot));
        note = wxString::Format(_L("Filaments changed: %s."), changed);
    }
    note += " " + _L("Slice again when ready.");
    m_note_sink(note, colour);
}

void StatusRow::show_overflow_menu()
{
    std::vector<HeaderMenuItem> menu{
        {project_summary().BeforeFirst('\n'),HeaderIcon::None,{},false,false,{}},
        // Version history, export, details and the print count live in the
        // Project tab of the Plates / Project pane.
        {_L("Recent projects…"),HeaderIcon::None,{},m_autosave != nullptr,true,[this] { show_recent_projects(); }},
        {_L("Back to Prepare"),HeaderIcon::Back,{},true,false,[this] { request_prepare(); }},
        {_L("Preferences…"),HeaderIcon::None,{},true,true,[] { wxGetApp().open_preferences(); }},
        {_L("Switch to classic view…"),HeaderIcon::None,{},true,true,
            [frame = wxWeakRef<wxWindow>(GetParent())] {
                // The menu invokes this on the header after dismissal. Detach
                // on the frame's next turn so the header's handler can return
                // before uninstall destroys it.
                if (frame) frame->CallAfter([frame] { if (frame) detach_shell(); });
            }}
    };
    if (m_autosave != nullptr && m_autosave->state() == Workspace::ProjectAutosave::State::Failed)
        menu.insert(menu.begin() + 1, {_L("Changes couldn't be saved. JusPrin will retry."),HeaderIcon::None,{},false,false,{}});
    if (m_autosave != nullptr && !m_autosave->warning().empty())
        menu.insert(menu.begin() + 1, {_L("Some older project data couldn't be removed."),HeaderIcon::None,{},false,false,{}});
    (new HeaderMenu(this,m_theme,m_dark,std::move(menu)))->open(*m_overflow_button);
}

void StatusRow::show_recent_projects(std::size_t offset)
{
    if (m_autosave == nullptr)
        return;
    try {
        if (!m_autosave->save_now())
            throw std::runtime_error(m_autosave->error());
        const auto projects = m_autosave->projects();
        std::vector<HeaderMenuItem> menu{
            {_L("Projects"),HeaderIcon::None,{},false,false,{}},
            {_L("Back"),HeaderIcon::Back,{},true,false,[this] { show_overflow_menu(); }}
        };
        const std::size_t end = std::min(projects.size(), offset + 8);
        for (std::size_t index = offset; index < end; ++index) {
            const auto& project = projects[index];
            wxString label = project.name.empty() || project.name == wxString(_L("Untitled")) ?
                _L("Untitled project") : wxString::FromUTF8(project.name);
            label += "  " + wxString::FromUTF8(project.updated_at);
            if (project.id == m_persistence.document().project_id())
                label += _L(" (current)");
            menu.push_back({label,HeaderIcon::None,{},true,false,[this, id = project.id] {
                if (!m_autosave->open_managed_project(id))
                    show_project_error(this, _L("Couldn't open project"),
                                       _L("The project couldn't be opened. Try again."), m_autosave->error());
                refresh();
            }});
        }
        if (end < projects.size())
            menu.push_back({_L("Older projects…"),HeaderIcon::None,{},true,false,[this, end] { show_recent_projects(end); }});
        (new HeaderMenu(this,m_theme,m_dark,std::move(menu)))->open(*m_overflow_button);
    } catch (const std::exception& error) {
        show_project_error(this, _L("Couldn't load projects"),
                           _L("Projects couldn't be loaded. Try again."), error.what());
    }
}

void StatusRow::request_action(PrintAction action)
{
    const auto actions = primary_print_action(action_state());
    const auto eligible = [action](const PrintActionItem& item) { return item.action == action && item.enabled; };
    if (!eligible(actions.primary) && std::none_of(actions.menu.begin(), actions.menu.end(), eligible)) return;
    switch (action) {
    case PrintAction::Slice: request_slice(); return;
    case PrintAction::SliceAll: request_slice(true); return;
    case PrintAction::Cancel: m_plater.cancel_slicing(); return;
    case PrintAction::CheckPrint: {
        const auto identity = print_target_identity();
        CheckPrintDialog dialog(this, m_plater, [this,identity] {
            return action_state().sliced && print_target_identity() == identity;
        });
        dialog.ShowModal();
        m_menu_button->SetFocus();
        refresh();
        return;
    }
    case PrintAction::Print:
    case PrintAction::PrintAll: {
        auto& presets = *wxGetApp().preset_bundle;
        const auto* host = presets.printers.get_edited_preset().config.option<ConfigOptionString>("print_host");
        if (!presets.use_bbl_network() && (!host || host->value.empty())) {
            wxMessageBox(_L("Configure a printer connection in Printer settings before printing. You can also export the sliced file."),
                         _L("Printer connection required"), wxOK | wxICON_INFORMATION, this);
            return;
        }
        SimpleEvent event(action == PrintAction::Print ? EVT_GLTOOLBAR_PRINT_PLATE : EVT_GLTOOLBAR_PRINT_ALL);
        m_plater.GetEventHandler()->ProcessEvent(event); // native confirmation; never an upload shortcut
        return;
    }
    case PrintAction::Export: {
        SimpleEvent event(EVT_GLTOOLBAR_EXPORT_SLICED_FILE);
        m_plater.GetEventHandler()->ProcessEvent(event);
        return;
    }
    }
}

void StatusRow::request_slice(bool all)
{
    const auto state = action_state();
    if (state.slicing || state.sliced || (all ? !state.can_slice_all : !state.can_slice)) return;
    m_plater.exit_gizmo();
    m_plater.update(true, true);
    request_prepare();
    SimpleEvent event(all ? EVT_GLTOOLBAR_SLICE_ALL : EVT_GLTOOLBAR_SLICE_PLATE);
    m_plater.GetEventHandler()->ProcessEvent(event);
    refresh();
}

void StatusRow::request_prepare()
{
    m_tabpanel.SetSelection(MainFrame::tp3DEditor);
}

} // namespace Slic3r::GUI::JusPrin
