#include "StatusRow.hpp"
#include "HeaderControls.hpp"
#include "PrinterMenu.hpp"
#include "PrinterSpoolChip.hpp"
#include "SetupCommands.hpp"
#include "SpoolMenu.hpp"
#include "SliceReviewPanel.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"
#include "slic3r/GUI/Event.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Notebook.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/ParamsDialog.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/BackgroundSlicingProcess.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/msgdlg.h>
#include <wx/weakref.h>
#include <wx/dcbuffer.h>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>
#include <algorithm>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin {

namespace {

#ifdef __WXMSW__
const wxEventTypeTag<wxBookCtrlEvent>& page_changed_event() { return wxEVT_BOOKCTRL_PAGE_CHANGED; }
#else
const wxEventTypeTag<wxBookCtrlEvent>& page_changed_event() { return wxEVT_NOTEBOOK_PAGE_CHANGED; }
#endif

} // namespace

StatusRow::StatusRow(wxWindow*                  parent,
                     const ShellTheme&          theme,
                     Plater&                    plater,
                     Notebook&                  tabpanel,
                     Agent::ProjectPersistence& persistence,
                     std::shared_ptr<Workspace::SliceReviews> reviews)
    : wxPanel(parent, wxID_ANY)
    , m_theme(theme)
    , m_plater(plater)
    , m_tabpanel(tabpanel)
    , m_persistence(persistence)
    , m_reviews(std::move(reviews))
{
    SetName("Project header");
    Workspace::SpoolStore::Config spool_config;
    spool_config.file_path = (boost::filesystem::path(data_dir()) / "jusprin" / "spools.json").string();
    m_spools = std::make_unique<Workspace::SpoolStore>(std::move(spool_config));
    if (m_spools->corrupt()) {
        // Visible, and the person's own file is preserved next to it. The chip
        // still works: it reseeds from whatever the project has loaded.
        BOOST_LOG_TRIVIAL(error) << "JusPrin: spools.json was damaged and has been moved aside: "
                                 << m_spools->corrupt_reason();
    }
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(-1, FromDIP(56)));
    m_home_button = new HeaderButton(this, theme, HeaderStyle::Quiet, _L("Home"), HeaderIcon::Back);
    m_home_button->SetName("Home navigation");
    m_chip = new PrinterSpoolChip(this, theme);
    m_slice_button = new HeaderButton(this, theme, HeaderStyle::PrimaryLeft, _L("Slice"), HeaderIcon::Slice);
    m_slice_button->SetName("Next print action");
    m_menu_button = new HeaderButton(this, theme, HeaderStyle::PrimaryRight, wxEmptyString, HeaderIcon::Down);
    m_menu_button->SetName(_L("Print actions"));
    m_overflow_button = new HeaderButton(this, theme, HeaderStyle::Outline, wxEmptyString, HeaderIcon::More);
    m_overflow_button->SetName("Project actions");
    m_overflow_button->SetToolTip(_L("Project details and preferences"));
    m_home_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { request_home(); });
    m_chip->on_printer_activated([this] { open_printer_menu(); });
    m_chip->on_spool_activated([this] { open_spool_menu(); });
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
    // A recorded print and a replaced document both change the print count
    // without publishing a project-state change, so the ledger is observed
    // separately. Persistence outlives this row in both teardown orders, and
    // the destructor clears the slot.
    m_persistence.set_ledger_listener([this]() { refresh(); });
    m_reviews->set_listener([this]() { refresh(); });
    m_plater.Bind(EVT_SLICE_STATUS_CHANGED, &StatusRow::on_slice_status_changed, this);
    m_plater.Bind(EVT_SLICING_UPDATE, &StatusRow::on_slicing_progress, this);
    m_tabpanel.Bind(page_changed_event(), &StatusRow::on_tab_changed, this);
    // The tab panel and this row are siblings, so wx may destroy either one
    // first at shutdown; only unbind while the tab panel still exists.
    m_tabpanel.Bind(wxEVT_DESTROY, &StatusRow::on_tabpanel_destroyed, this);
}

StatusRow::~StatusRow()
{
    m_persistence.set_ledger_listener(nullptr);
    m_reviews->set_listener(nullptr);
    if (m_tabpanel_alive) {
        m_plater.Unbind(EVT_SLICE_STATUS_CHANGED, &StatusRow::on_slice_status_changed, this);
        m_plater.Unbind(EVT_SLICING_UPDATE, &StatusRow::on_slicing_progress, this);
        m_tabpanel.Unbind(wxEVT_DESTROY, &StatusRow::on_tabpanel_destroyed, this);
        m_tabpanel.Unbind(page_changed_event(), &StatusRow::on_tab_changed, this);
    }
}

void StatusRow::on_slice_status_changed(wxCommandEvent& event)
{
    refresh();
    event.Skip();
}

void StatusRow::on_slicing_progress(SlicingStatusEvent& event)
{
    // Let Orca update the owning PartPlate first. This observes progress; it
    // never changes worker state or replaces the native slicing handler.
    event.Skip();
    m_plater.CallAfter([self=wxWeakRef<StatusRow>(this)] {
        if (self && self->m_tabpanel_alive) self->refresh_workspace_status();
    });
}

void StatusRow::on_tab_changed(wxBookCtrlEvent& event)
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
    for (auto* button : {m_home_button,m_slice_button,m_menu_button,m_overflow_button})
        button->set_dark(dark);
    m_chip->set_dark(dark);
    refresh_workspace_status();
    Refresh();
}

void StatusRow::refresh()
{
    refresh_chip();
    m_overflow_button->SetToolTip(project_summary());
    m_home_button->SetLabel(m_tabpanel.GetSelection() == MainFrame::tpHome ? _L("Prepare") : _L("Home"));

    const auto state = action_state();
    const auto actions = primary_print_action(state);
    m_slice_button->SetLabel(action_label(actions.primary.action, true));
    m_slice_button->Enable(actions.primary.enabled);
    m_slice_button->set_icon(actions.primary.action == PrintAction::CheckPrint ? HeaderIcon::Eye :
                            actions.primary.action == PrintAction::Print ? HeaderIcon::Print : HeaderIcon::Slice);
    m_slice_button->set_status(!state.slicing && actions.primary.enabled, state.needs_review);
    m_menu_button->set_icon(state.slicing ? HeaderIcon::Cancel : HeaderIcon::Down);
    m_menu_button->SetToolTip(state.slicing ? _L("Cancel slicing") : _L("Print actions"));
    m_menu_button->SetName(state.slicing ? _L("Cancel slicing") : _L("Print actions"));
    // An empty menu has no half to show; the primary keeps its full silhouette.
    m_menu_button->Show(!actions.menu.empty());
    m_slice_button->set_attached(!actions.menu.empty());
    layout_header();
    refresh_workspace_status();
}

wxWindow* StatusRow::create_workspace_status(wxWindow* parent)
{
    auto* panel = new wxPanel(parent);
    m_workspace_status = panel;
    auto* vertical = new wxBoxSizer(wxVERTICAL);
    auto* review = new SliceReviewPanel(panel,m_theme,[self=wxWeakRef<StatusRow>(this)](
        Workspace::SliceIdentity identity, const std::vector<std::string>& displayed) {
        if (self && self->m_tabpanel_alive && self->m_tabpanel.GetSelection() == MainFrame::tpPreview &&
            self->m_plater.is_preview_shown() && identity == self->slice_identity()) {
            self->m_reviews->acknowledge_displayed(identity,displayed);
            return true;
        }
        return false;
    });
    m_review_panel = review;
    vertical->Add(review,0,wxEXPAND);
    auto* strip = new wxBoxSizer(wxHORIZONTAL);
    auto* label = new wxStaticText(panel,wxID_ANY,wxEmptyString,wxDefaultPosition,wxDefaultSize,wxST_ELLIPSIZE_END);
    label->SetName("Active plate status"); label->SetFont(Label::Body_12);
    m_plate_label = label;
    strip->Add(label,1,wxALIGN_CENTER_VERTICAL | wxLEFT,FromDIP(16));
    auto* back = new HeaderButton(panel,m_theme,HeaderStyle::Quiet,_L("Back to Prepare"),HeaderIcon::Back);
    back->Bind(wxEVT_BUTTON,[self=wxWeakRef<StatusRow>(this)](wxCommandEvent&) { if (self) self->request_prepare(); });
    m_return_button = back;
    strip->Add(back,0,wxRIGHT,FromDIP(8));
    strip->SetMinSize(FromDIP(wxSize(-1,34)));
    vertical->Add(strip,0,wxEXPAND);
    panel->SetSizer(vertical);
    refresh_workspace_status();
    return panel;
}

void StatusRow::refresh_workspace_status()
{
    if (!m_workspace_status || !m_tabpanel_alive) return;
    const auto& palette = m_theme.palette(m_dark);
    m_workspace_status->SetBackgroundColour(palette.surface_canvas);
    m_plate_label->SetForegroundColour(palette.text_secondary);
    m_return_button->set_dark(m_dark);
    m_review_panel->set_dark(m_dark);
    const auto state = action_state();
    const bool workspace = m_tabpanel.GetSelection() == MainFrame::tp3DEditor || m_tabpanel.GetSelection() == MainFrame::tpPreview;
    bool layout = m_workspace_status->Show(workspace);
    const bool preview = m_tabpanel.GetSelection() == MainFrame::tpPreview;
    const bool check = preview && state.preview && state.sliced && !state.slicing;
    layout = m_review_panel->Show(check) || layout;
    layout = m_return_button->Show(preview) || layout;
    if (check) m_review_panel->set_report(slice_identity(),m_reviews->findings(slice_identity()));
    auto* plate = m_plater.get_partplate_list().get_curr_plate();
    wxString label = wxString::Format(_L("Plate %d"),state.plate_number);
    if (plate && !plate->get_plate_name().empty()) label += " · " + wxString::FromUTF8(plate->get_plate_name());
    if (state.slicing) label += " · " + _L("Slicing…") + wxString::Format(" %.0f%%",plate ? std::clamp(double(plate->get_slicing_percent()),0.,100.) : 0.);
    else label += " · " + (state.sliced ? _L("sliced") : _L("not sliced"));
    m_plate_label->SetLabel(label);
    m_workspace_status->Layout();
    if (layout) m_workspace_status->GetParent()->Layout();
}

Workspace::SliceIdentity StatusRow::slice_identity() const
{
    auto* plate = m_plater.get_partplate_list().get_curr_plate();
    return {m_plater.project_state_session(), plate ? plate->id().id : 0,
            plate && plate->is_slice_result_valid() && !m_plater.is_background_process_slicing() && plate->get_slice_result() ?
                plate->get_slice_result()->id : 0};
}

PrintActionState StatusRow::action_state() const
{
    PrintActionState state;
    auto& plates = m_plater.get_partplate_list();
    auto* plate = plates.get_curr_plate();
    state.slicing = m_plater.is_background_process_slicing();
    state.plate_number = plates.get_curr_plate_index() + 1;
    state.plate_count = plates.get_plate_count();
    state.preview = m_plater.is_preview_shown();
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
    state.needs_review = m_reviews->needs_review(slice_identity());
    return state;
}

wxString StatusRow::action_label(PrintAction action, bool primary) const
{
    const auto state = action_state();
    switch (action) {
    case PrintAction::Slice: return _L("Slice");
    case PrintAction::SliceAll: return _L("Slice all plates");
    case PrintAction::Cancel: return primary ? _L("Slicing…") : _L("Cancel");
    case PrintAction::CheckPrint: return _L("Check print");
    case PrintAction::Print: return primary ? (state.plate_count > 1 ? wxString::Format(_L("Print plate %d"), state.plate_number) : _L("Print")) : _L("Print…");
    case PrintAction::PrintAll: return _L("Print all plates…");
    case PrintAction::Export: return _L("Export sliced file…");
    case PrintAction::Prepare: return _L("Back to Prepare");
    }
    throw std::logic_error("Unknown print action");
}

void StatusRow::show_action_menu()
{
    const auto identity = slice_identity();
    const auto state = action_state();
    const auto actions = primary_print_action(state);
    std::vector<HeaderMenuItem> menu;
    for (const auto& item : actions.menu) {
        HeaderIcon icon = HeaderIcon::Slice;
        if (item.action == PrintAction::CheckPrint) icon = HeaderIcon::Eye;
        else if (item.action == PrintAction::Print) icon = HeaderIcon::Print;
        else if (item.action == PrintAction::PrintAll || item.action == PrintAction::SliceAll) icon = HeaderIcon::Plates;
        else if (item.action == PrintAction::Export) icon = HeaderIcon::Export;
        else if (item.action == PrintAction::Prepare) icon = HeaderIcon::Back;
        else if (item.action == PrintAction::Cancel) icon = HeaderIcon::Cancel;
        wxString detail;
        if (item.action == PrintAction::PrintAll)
            detail = wxString::Format(_L("%d sliced"), state.sliced_count);
        menu.push_back({action_label(item.action), icon, detail, item.enabled, item.action == PrintAction::Export || item.action == PrintAction::Prepare,
            [this,identity,action=item.action] {
                // The menu describes one plate/result. A later plate switch or
                // project replacement must not retarget that displayed command.
                if (slice_identity() == identity) request_action(action);
            }});
    }
    if (!menu.empty()) (new HeaderMenu(this,m_theme,m_dark,std::move(menu)))->open(*m_menu_button);
}

void StatusRow::layout_header()
{
    const int margin = FromDIP(16), gap = FromDIP(8), height = GetClientSize().y;
    auto place = [height](HeaderButton* button, int x, int width = -1) {
        wxSize size = button->GetBestSize();
        if (width >= 0) size.x = width;
        button->SetSize(x,(height-size.y)/2,size.x,size.y);
    };
    int right = GetClientSize().x-margin-m_overflow_button->GetBestSize().x;
    place(m_overflow_button,right);
    right -= gap;
    if (m_menu_button->IsShown()) {
        right -= m_menu_button->GetBestSize().x;
        place(m_menu_button,right);
    }
    right -= m_slice_button->GetBestSize().x;
    place(m_slice_button,right);
    place(m_home_button,margin);
    const int left = margin+m_home_button->GetBestSize().x+gap;
    const int available = std::max(0,right-gap-left);
    const int width = std::min(m_chip->GetBestSize().x,available);
    const wxSize chip = m_chip->GetBestSize();
    m_chip->SetSize(left+(available-width)/2,(height-chip.y)/2,width,chip.y);
}

wxString StatusRow::project_summary() const
{
    wxString name = m_plater.get_project_name();
    if (name.empty()) name = _L("Untitled");
    if (m_plater.is_project_dirty()) name += " — " + _L("Unsaved changes");
    return name + "\n" + _L("Prints") + wxString::FromUTF8(" \xC2\xB7 ") +
           wxString::Format("%d",int(m_persistence.document().physical_print_count()));
}

void StatusRow::request_home()
{
    m_tabpanel.SetSelection(m_tabpanel.GetSelection() == MainFrame::tpHome ? MainFrame::tp3DEditor : MainFrame::tpHome);
}

std::optional<Workspace::Spool> StatusRow::current_spool()
{
    const auto printer  = SetupCommands::current_printer();
    const auto filament = SetupCommands::current_filament();
    if (!printer.valid || !filament.valid)
        return std::nullopt;
    const std::string colour = SetupCommands::current_colour().ToStdString();

    if (auto matched = m_spools->current(printer.preset_name, filament.preset_name, colour))
        return matched;
    // A printer with no remembered spools gets its first one from whatever the
    // project already has loaded, so the chip's right half is never empty and
    // the person starts with a list of one rather than a list of none. A
    // printer that already has spools and is on some other preset simply has
    // no current spool; guessing a new one on every project would fill the
    // list with entries nobody chose.
    if (!m_spools->spools_for(printer.preset_name).empty())
        return std::nullopt;

    Workspace::Spool seed;
    seed.printer_preset  = printer.preset_name;
    seed.filament_preset = filament.preset_name;
    seed.colour          = colour;
    seed.brand           = filament.vendor.ToStdString();
    const wxString word  = SetupCommands::colour_word(wxColour(SetupCommands::current_colour()));
    const wxString label = word.empty() ? filament.alias : word + " " + filament.material;
    seed.name            = (label.empty() ? filament.alias : label).ToStdString();
    return m_spools->add(std::move(seed));
}

// The chip renders Orca's authoritative selection plus the spool the store
// remembers for it. Nothing here writes; a swap goes through SetupCommands.
void StatusRow::refresh_chip()
{
    const auto printer = SetupCommands::current_printer();
    m_chip->set_printer(printer.nickname, printer.valid ? wxString::Format("%g", printer.nozzle) : wxString{});
    if (printer.extruder_count > 1) {
        // TODO(slice 2): multi-extruder printers show extruder 0 only. Slot
        // mapping, the AMS chip, and multi-colour projects are a separate
        // brief; the chip must not imply it is describing every extruder.
    }

    const auto     filament = SetupCommands::current_filament();
    const wxString colour   = SetupCommands::current_colour();
    const wxColour parsed(colour);
    const auto     spool    = current_spool();
    m_chip->set_spool(spool ? wxString::FromUTF8(spool->name) : filament.alias, parsed);
}

void StatusRow::open_printer_menu()
{
    PrinterMenu::open(this, m_theme, m_dark, m_plater, m_chip->printer_half());
}

void StatusRow::open_spool_menu()
{
    SpoolMenu::Host host;
    host.store  = m_spools.get();
    host.plater = &m_plater;
    host.selected = [self = wxWeakRef<StatusRow>(this)](const Workspace::Spool& spool) {
        if (self) self->on_spool_selected(spool);
    };
    host.store_changed = [self = wxWeakRef<StatusRow>(this)] { if (self) self->refresh(); };
    SpoolMenu::open(this, m_theme, m_dark, std::move(host), m_chip->spool_half());
}

// A swap changes three things and nothing else: the chip's right half and the
// pinned card both follow Orca's own settings-changed notification, and one
// grey line lands at the end of the thread. The line reports; it never offers
// to undo, because the selection belongs to the chip.
void StatusRow::on_spool_selected(const Workspace::Spool& spool)
{
    refresh();
    if (!m_note_sink) return;
    wxString note = wxString::Format(_L("Spool is now %s"), wxString::FromUTF8(spool.name));
    if (!spool.brand.empty()) note += wxString::Format(" (%s)", wxString::FromUTF8(spool.brand));
    note += ".";
    const auto filament = SetupCommands::current_filament();
    if (!filament.material.empty())
        note += " " + wxString::Format(_L("Heat and cooling follow %s; walls and infill unchanged."), filament.material);
    note += " " + _L("Slice again when ready.");
    m_note_sink(note);
}

bool StatusRow::select_spool(const std::string& spool_id)
{
    const auto printer = SetupCommands::current_printer();
    const auto spool   = m_spools->find(spool_id);
    // A spool belonging to another printer is not a stale menu row, it is a
    // wrong instruction; refusing it keeps the mistake visible to the caller.
    if (!spool || spool->printer_preset != printer.preset_name)
        return false;
    if (!SetupCommands::select_filament_preset(m_plater, spool->filament_preset))
        return false;
    if (const wxColour colour(wxString::FromUTF8(spool->colour)); colour.IsOk())
        SetupCommands::set_filament_colour(m_plater, colour);
    m_spools->touch(spool->id);
    on_spool_selected(*spool);
    return true;
}

std::vector<Workspace::Spool> StatusRow::listed_spools()
{
    // Consult current_spool() first so a printer with no spools seeds one,
    // exactly as opening the menu would.
    current_spool();
    return m_spools->spools_for(SetupCommands::current_printer().preset_name);
}

Workspace::Spool StatusRow::remember_spool(const std::string& filament_preset, const std::string& colour,
                                           const std::string& name)
{
    Workspace::Spool spool;
    spool.printer_preset  = SetupCommands::current_printer().preset_name;
    spool.filament_preset = filament_preset;
    spool.colour          = colour;
    spool.name            = name;
    spool.brand           = SetupCommands::current_filament().vendor.ToStdString();
    return m_spools->add(std::move(spool));
}

wxString StatusRow::printer_text() const { return m_chip->printer_half().GetLabel(); }
wxString StatusRow::spool_text() const { return m_chip->spool_half().GetLabel(); }

void StatusRow::show_overflow_menu()
{
    std::vector<HeaderMenuItem> menu{
        {project_summary().BeforeFirst('\n'),HeaderIcon::None,{},false,false,{}},
        {_L("Project details"),HeaderIcon::None,{},true,true,[this] { m_tabpanel.SetSelection(MainFrame::tpProject); }},
        {_L("Back to Prepare"),HeaderIcon::Back,{},true,false,[this] { request_prepare(); }},
        {project_summary().AfterFirst('\n'),HeaderIcon::None,{},false,false,{}},
        {_L("Preferences…"),HeaderIcon::None,{},true,true,[] { wxGetApp().open_preferences(); }}
    };
    (new HeaderMenu(this,m_theme,m_dark,std::move(menu)))->open(*m_overflow_button);
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
    case PrintAction::CheckPrint: request_check_print(); return;
    case PrintAction::Prepare: request_prepare(); return;
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

void StatusRow::request_check_print()
{
    const auto identity = slice_identity();
    if (!identity.valid()) return;
    m_tabpanel.SetSelection(MainFrame::tpPreview);
    // Preview selection is asynchronous. The report's actual paint callback
    // acknowledges the displayed findings; entering Preview alone does not.
    m_plater.CallAfter([self = wxWeakRef<StatusRow>(this)] {
        if (!self || !self->m_tabpanel_alive) return;
        self->refresh();
    });
}

void StatusRow::request_prepare()
{
    m_tabpanel.SetSelection(MainFrame::tp3DEditor);
}

} // namespace Slic3r::GUI::JusPrin
