#include "StatusRow.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/Event.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Notebook.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/StaticBox.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>

namespace Slic3r::GUI::JusPrin {

namespace {

#ifdef __WXMSW__
const wxEventTypeTag<wxBookCtrlEvent>& page_changed_event() { return wxEVT_BOOKCTRL_PAGE_CHANGED; }
#else
const wxEventTypeTag<wxBookCtrlEvent>& page_changed_event() { return wxEVT_NOTEBOOK_PAGE_CHANGED; }
#endif

} // namespace

StatusRow::StatusRow(wxWindow* parent, const ShellTheme& theme, Plater& plater, Notebook& tabpanel)
    : wxPanel(parent, wxID_ANY)
    , m_theme(theme)
    , m_plater(plater)
    , m_tabpanel(tabpanel)
{
    auto* sizer = new wxBoxSizer(wxHORIZONTAL);

    m_project_name = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                      wxST_ELLIPSIZE_MIDDLE);
    m_project_name->SetFont(Label::Head_14);

    m_dirty_marker = new wxStaticText(this, wxID_ANY, wxString::FromUTF8("\xE2\x80\xA2"));
    m_dirty_marker->SetFont(Label::Head_14);
    m_dirty_marker->SetToolTip(_L("Unsaved changes"));

    // Setup pointer styled as the design's toolbar chip: printer, material,
    // and active plate in a quiet bordered container on the right.
    m_setup_chip = new StaticBox(this);
    m_setup_chip->SetCornerRadius(FromDIP(4));
    m_setup_summary = new wxStaticText(m_setup_chip, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                       wxST_ELLIPSIZE_END);
    m_setup_summary->SetFont(Label::Body_12);
    auto* chip_sizer = new wxBoxSizer(wxHORIZONTAL);
    chip_sizer->Add(m_setup_summary, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(10));
    m_setup_chip->SetSizer(chip_sizer);
    m_setup_chip->SetMinSize(wxSize(-1, FromDIP(28)));

    // The design's centered action group: Slice (primary), Check print
    // (secondary mode toggle), Print (secondary; enabled in a later phase).
    m_slice_button = new Button(this, _L("Slice"));
    m_mode_button  = new Button(this, _L("Check print"));
    m_print_button = new Button(this, _L("Print"));
    for (Button* button : {m_slice_button, m_mode_button, m_print_button}) {
        button->SetFont(Label::Body_12);
        button->SetCornerRadius(FromDIP(8));
        button->SetPaddingSize(wxSize(FromDIP(12), FromDIP(6)));
    }
    m_print_button->Enable(false);
    m_print_button->SetToolTip(_L("Print preflight arrives in a later JusPrin release"));
    m_print_button->EnableTooltipEvenDisabled();

    sizer->AddSpacer(FromDIP(16));
    sizer->Add(m_project_name, 0, wxALIGN_CENTER_VERTICAL);
    sizer->AddSpacer(FromDIP(4));
    sizer->Add(m_dirty_marker, 0, wxALIGN_CENTER_VERTICAL);
    sizer->AddStretchSpacer(1);
    sizer->Add(m_slice_button, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, FromDIP(8));
    sizer->AddSpacer(FromDIP(8));
    sizer->Add(m_mode_button, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, FromDIP(8));
    sizer->AddSpacer(FromDIP(8));
    sizer->Add(m_print_button, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, FromDIP(8));
    sizer->AddStretchSpacer(1);
    sizer->Add(m_setup_chip, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, FromDIP(8));
    sizer->AddSpacer(FromDIP(16));
    SetSizer(sizer);

    m_slice_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { request_slice(); });
    m_mode_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (m_plater.is_preview_shown())
            request_prepare();
        else
            request_check_print();
    });

    Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& event) {
        apply_appearance(GUI_App::dark_mode());
        event.Skip();
    });

    m_project_state_subscription = m_plater.subscribe_project_state(
        [this](const ProjectStateChanged&) { refresh(); });
    m_tabpanel.Bind(page_changed_event(), &StatusRow::on_tab_changed, this);
    // The tab panel and this row are siblings, so wx may destroy either one
    // first at shutdown; only unbind while the tab panel still exists.
    m_tabpanel.Bind(wxEVT_DESTROY, &StatusRow::on_tabpanel_destroyed, this);
}

StatusRow::~StatusRow()
{
    if (m_tabpanel_alive) {
        m_tabpanel.Unbind(wxEVT_DESTROY, &StatusRow::on_tabpanel_destroyed, this);
        m_tabpanel.Unbind(page_changed_event(), &StatusRow::on_tab_changed, this);
    }
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
    m_project_name->SetForegroundColour(palette.text_primary);
    m_dirty_marker->SetForegroundColour(palette.status_warning);
    m_setup_summary->SetForegroundColour(palette.text_secondary);

    m_slice_button->SetBackgroundColor(StateColor(
        std::pair<wxColour, int>(palette.action_disabled, StateColor::Disabled),
        std::pair<wxColour, int>(palette.action_primary_pressed, StateColor::Pressed),
        std::pair<wxColour, int>(palette.action_primary_hover, StateColor::Hovered),
        std::pair<wxColour, int>(palette.action_primary, StateColor::Normal)));
    m_slice_button->SetBorderColor(palette.action_primary);
    m_slice_button->SetTextColor(StateColor(
        std::pair<wxColour, int>(palette.action_disabled_text, StateColor::Disabled),
        std::pair<wxColour, int>(palette.action_primary_text, StateColor::Normal)));

    for (Button* button : {m_mode_button, m_print_button}) {
        button->SetBackgroundColor(StateColor(
            std::pair<wxColour, int>(palette.action_disabled, StateColor::Disabled),
            std::pair<wxColour, int>(palette.action_secondary_pressed, StateColor::Pressed),
            std::pair<wxColour, int>(palette.action_secondary_hover, StateColor::Hovered),
            std::pair<wxColour, int>(palette.action_secondary, StateColor::Normal)));
        button->SetBorderColor(palette.action_secondary_border);
        button->SetTextColor(StateColor(
            std::pair<wxColour, int>(palette.action_disabled_text, StateColor::Disabled),
            std::pair<wxColour, int>(palette.action_secondary_text, StateColor::Normal)));
    }

    m_setup_chip->SetBackgroundColor(palette.surface_subtle);
    m_setup_chip->SetBorderColor(palette.border_subtle);
    m_setup_summary->SetBackgroundColour(palette.surface_subtle);

    Refresh();
}

void StatusRow::refresh()
{
    wxString project_name = m_plater.get_project_name();
    if (project_name.IsEmpty())
        project_name = _L("Untitled");
    m_project_name->SetLabel(project_name);
    m_dirty_marker->Show(m_plater.is_project_dirty());

    const PresetBundle* presets = wxGetApp().preset_bundle;
    wxString summary;
    if (presets != nullptr) {
        summary = wxString::FromUTF8(presets->printers.get_selected_preset().label(false));
        if (!presets->filament_presets.empty()) {
            summary += wxString::FromUTF8("  \xC2\xB7  ");
            summary += wxString::FromUTF8(presets->filament_presets.front());
        }
    }
    PartPlateList& plates = m_plater.get_partplate_list();
    if (plates.get_plate_count() > 0) {
        summary += wxString::FromUTF8("  \xC2\xB7  ");
        summary += wxString::Format(_L("Plate %d/%d"), plates.get_curr_plate_index() + 1, plates.get_plate_count());
    }
    m_setup_summary->SetLabel(summary);
    m_setup_chip->SetMinSize(wxSize(m_setup_summary->GetBestSize().GetWidth() + FromDIP(20), FromDIP(28)));

    m_slice_button->Enable(!m_plater.model().objects.empty() && !m_plater.only_gcode_mode());
    update_mode_button();
    Layout();
}

void StatusRow::update_mode_button()
{
    m_mode_button->SetLabel(m_plater.is_preview_shown() ? _L("Back to Prepare") : _L("Check print"));
}

void StatusRow::request_slice()
{
    if (m_plater.model().objects.empty() || m_plater.only_gcode_mode())
        return;
    // Same dispatch as the stock slice shortcut in MainFrame: the slicing
    // behavior itself stays owned by Plater's EVT_GLTOOLBAR_SLICE_PLATE path.
    m_plater.exit_gizmo();
    m_plater.update(true, true);
    wxPostEvent(&m_plater, SimpleEvent(EVT_GLTOOLBAR_SLICE_PLATE));
    m_tabpanel.SetSelection(MainFrame::tpPreview);
}

void StatusRow::request_check_print()
{
    m_tabpanel.SetSelection(MainFrame::tpPreview);
}

void StatusRow::request_prepare()
{
    m_tabpanel.SetSelection(MainFrame::tp3DEditor);
}

} // namespace Slic3r::GUI::JusPrin
