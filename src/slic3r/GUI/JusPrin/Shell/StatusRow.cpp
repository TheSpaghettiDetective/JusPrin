#include "StatusRow.hpp"
#include "HeaderControls.hpp"

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
#include <wx/msgdlg.h>
#include <wx/weakref.h>
#include <wx/dcbuffer.h>
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
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(-1, FromDIP(56)));
    m_home_button = new HeaderButton(this, theme, HeaderStyle::Quiet, _L("Home"), HeaderIcon::Back);
    m_home_button->SetName("Home navigation");
    m_setup_chip = new HeaderButton(this, theme, HeaderStyle::Setup, wxEmptyString, HeaderIcon::Machine);
    m_setup_chip->SetName("Printer setup");
    m_slice_button = new HeaderButton(this, theme, HeaderStyle::PrimaryLeft, _L("Slice"), HeaderIcon::Slice);
    m_slice_button->SetName("Next print action");
    m_menu_button = new HeaderButton(this, theme, HeaderStyle::PrimaryRight, wxEmptyString, HeaderIcon::Down);
    m_menu_button->SetName(_L("Print actions"));
    m_overflow_button = new HeaderButton(this, theme, HeaderStyle::Outline, wxEmptyString, HeaderIcon::More);
    m_overflow_button->SetName("Project actions");
    m_overflow_button->SetToolTip(_L("Project details and preferences"));
    m_home_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { request_home(); });
    m_setup_chip->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { show_setup_menu(); });
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
        m_tabpanel.Unbind(wxEVT_DESTROY, &StatusRow::on_tabpanel_destroyed, this);
        m_tabpanel.Unbind(page_changed_event(), &StatusRow::on_tab_changed, this);
    }
}

void StatusRow::on_slice_status_changed(wxCommandEvent& event)
{
    refresh();
    event.Skip();
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
    for (auto* button : {m_home_button,m_setup_chip,m_slice_button,m_menu_button,m_overflow_button})
        button->set_dark(dark);
    Refresh();
}

void StatusRow::refresh()
{
    const PresetBundle* presets = wxGetApp().preset_bundle;
    wxString summary;
    std::vector<wxColour> colors;
    if (presets != nullptr) {
        const auto& printer = presets->printers.get_edited_preset();
        summary = wxString::FromUTF8(printer.config.opt_string("printer_model"));
        if (summary.empty()) {
            summary = wxString::FromUTF8(printer.label(false));
            // Custom profiles often encode the nozzle in their display name.
            const auto nozzle_suffix = summary.Find(" nozzle");
            if (nozzle_suffix != wxNOT_FOUND) summary = summary.Left(nozzle_suffix).BeforeLast(' ');
        }
        if (auto* nozzle = printer.config.option<ConfigOptionFloats>("nozzle_diameter"); nozzle && !nozzle->values.empty())
            summary += wxString::FromUTF8(" \xC2\xB7 ") + wxString::Format("%g",nozzle->values.front());
        if (!presets->filament_presets.empty()) {
            const auto* filament = presets->filaments.find_preset(presets->filament_presets.front());
            wxString material = wxString::FromUTF8(filament && !filament->alias.empty() ? filament->alias : presets->filament_presets.front());
            material = material.BeforeFirst('@'); material.Trim();
            summary += wxString::FromUTF8(" \xC2\xB7 ") + material;
        }
        if (const auto* configured = presets->project_config.option<ConfigOptionStrings>("filament_colour"))
            for (const auto& color : configured->values) {
                wxColour parsed(wxString::FromUTF8(color));
                colors.push_back(parsed.IsOk() ? parsed : m_theme.palette(m_dark).text_secondary);
            }
    }
    if (summary.empty()) summary = _L("Select printer");
    m_setup_chip->SetLabel(summary);
    m_setup_chip->set_slots(std::move(colors));
    m_setup_chip->SetToolTip(summary + "\n" + _L("Configured materials — verify loaded spools before printing"));
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
    m_menu_button->Enable(!actions.menu.empty());
    layout_header();
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
    case PrintAction::PrintAll: return _L("Print all plates") + "  " +
        (state.plate_count > 1 ? wxString::Format(_L("%d sliced"), state.sliced_count) : _L("1 plate"));
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
        wxString label = action_label(item.action);
        if (item.action == PrintAction::PrintAll) {
            label = _L("Print all plates");
            detail = wxString::Format(_L("%d sliced"), state.sliced_count);
        }
        menu.push_back({label, icon, detail, item.enabled, item.action == PrintAction::Export || item.action == PrintAction::Prepare,
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
    right -= gap+m_menu_button->GetBestSize().x;
    place(m_menu_button,right);
    right -= m_slice_button->GetBestSize().x;
    place(m_slice_button,right);
    place(m_home_button,margin);
    const int left = margin+m_home_button->GetBestSize().x+gap;
    const int available = std::max(0,right-gap-left);
    const int width = std::min(m_setup_chip->GetBestSize().x,available);
    place(m_setup_chip,left+(available-width)/2,width);
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

void StatusRow::show_setup_menu()
{
    auto edit = [](Preset::Type type) {
        auto* tab = wxGetApp().get_tab(type);
        // Follow PlaterPresetComboBox::switch_to_tab: the Tab activates its
        // own page and ParamsPanel, including clearing the previous page.
        wxGetApp().params_dialog()->Popup();
        tab->OnActivate();
        tab->restore_last_select_item();
    };
    std::vector<HeaderMenuItem> menu{
        {_L("Printer and nozzle…"),HeaderIcon::Machine,{},true,false,[edit] { edit(Preset::TYPE_PRINTER); }},
        {_L("Filament…"),HeaderIcon::Plates,{},true,false,[edit] { edit(Preset::TYPE_FILAMENT); }}
    };
    (new HeaderMenu(this,m_theme,m_dark,std::move(menu)))->open(*m_setup_chip);
}

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
    // Notebook selection and the native canvas switch do not finish in the
    // same callback. Acknowledge after that event, only if the requested
    // result is still the one actually shown (not after a rapid Back/edit).
    // Queue on Plater, like MainFrame's Preview command, so the two callbacks
    // keep their order even when other wx event handlers already have work.
    m_plater.CallAfter([self = wxWeakRef<StatusRow>(this), identity] {
        if (!self || !self->m_tabpanel_alive) return;
        if (self->m_tabpanel.GetSelection() == MainFrame::tpPreview && self->m_plater.is_preview_shown() && identity == self->slice_identity())
            self->m_reviews->acknowledge(identity);
        self->refresh();
    });
}

void StatusRow::request_prepare()
{
    m_tabpanel.SetSelection(MainFrame::tp3DEditor);
}

} // namespace Slic3r::GUI::JusPrin
