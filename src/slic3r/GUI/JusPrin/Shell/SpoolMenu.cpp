#include "SpoolMenu.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"

#include <wx/sizer.h>
#include <wx/weakref.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>
#include <wx/colordlg.h>

#include <algorithm>

namespace Slic3r::GUI::JusPrin {

namespace {

constexpr int kSwatchesPerRow = 6;

HeaderMenuItem separator()
{
    HeaderMenuItem item;
    item.separator = true;
    item.title     = true;
    return item;
}

HeaderMenuItem title_row(const wxString& text)
{
    HeaderMenuItem item;
    item.title = true;
    item.label = text;
    return item;
}

wxString middot() { return wxString::FromUTF8(" \xC2\xB7 "); }

// "Cold White · eSun" as one row label; the brand reads as secondary because
// the row paints everything after the separator in text/secondary would be a
// second text run. Keeping it in the label keeps ellipsis behaviour correct,
// and the brand is what gets dropped first when the name is long.
wxString spool_label(const Workspace::Spool& spool)
{
    wxString label = wxString::FromUTF8(spool.name);
    if (!spool.brand.empty()) label += middot() + wxString::FromUTF8(spool.brand);
    return label;
}

bool matches(const SetupCommands::FilamentInfo& filament, const wxString& query)
{
    if (query.empty()) return true;
    const wxString needle = query.Lower();
    return filament.alias.Lower().Contains(needle) || filament.vendor.Lower().Contains(needle);
}

} // namespace

SpoolMenu::SpoolMenu(wxWindow* owner, const ShellTheme& theme, bool dark, Host host)
    : m_owner(owner), m_theme(theme), m_dark(dark), m_host(std::move(host))
{}

void SpoolMenu::open(wxWindow* owner, const ShellTheme& theme, bool dark, Host host, HeaderButton& anchor)
{
    auto* controller = new SpoolMenu(owner, theme, dark, std::move(host));
    controller->m_menu = new HeaderMenu(owner, theme, dark, {});
    controller->m_menu->set_dismiss_listener([controller] { delete controller; });
    controller->show_spools();
    controller->m_menu->open(anchor);
}

void SpoolMenu::select(const Workspace::Spool& spool)
{
    // Order matters: the preset switch first, because it can pull a colour of
    // its own from the preset, then the spool's colour over the top, then the
    // recency stamp. The chip refreshes from Orca's own settings-changed
    // notification, not from here.
    if (!SetupCommands::select_filament_preset(*m_host.plater, spool.filament_preset))
        return;
    const wxColour colour(wxString::FromUTF8(spool.colour));
    if (colour.IsOk())
        SetupCommands::set_filament_colour(*m_host.plater, colour);
    m_host.store->touch(spool.id);
    if (m_host.selected) m_host.selected(spool);
}

void SpoolMenu::show_spools()
{
    m_menu->set_header_builder(nullptr);
    const auto printer = SetupCommands::current_printer();
    const auto current = m_host.store->current(printer.preset_name,
                                               SetupCommands::current_filament().preset_name,
                                               SetupCommands::current_colour().ToStdString());

    std::vector<HeaderMenuItem> rows;
    rows.push_back(title_row(wxString::Format(_L("SPOOLS ON %s"), printer.nickname.Upper())));

    // Never truncated and never scrolled: a person keeps three to fifteen
    // spools, and the whole design rests on the right one being one click away.
    for (const Workspace::Spool& spool : m_host.store->spools_for(printer.preset_name)) {
        HeaderMenuItem row;
        row.label            = spool_label(spool);
        row.decoration.dot   = wxColour(wxString::FromUTF8(spool.colour));
        row.decoration.check = current.has_value() && current->id == spool.id;
        row.invoke           = [this, spool] { select(spool); };
        row.row_action       = HeaderIcon::More;
        row.invoke_row_action = [this, spool] { show_row_menu(spool); };
        rows.push_back(std::move(row));
    }

    HeaderMenuItem other;
    other.separator          = true;
    other.label              = _L("Other spool…");
    other.decoration.trailing = HeaderIcon::Right;
    other.keeps_open         = true;
    other.invoke             = [this] { show_other_spool(); };
    rows.push_back(std::move(other));

    m_menu->replace_items(std::move(rows));
}

void SpoolMenu::show_row_menu(const Workspace::Spool& spool)
{
    m_menu->set_header_builder(nullptr);
    std::vector<HeaderMenuItem> rows;

    HeaderMenuItem back;
    back.label      = wxString::FromUTF8(spool.name);
    back.icon       = HeaderIcon::Back;
    back.keeps_open = true;
    back.invoke     = [this] { show_spools(); };
    rows.push_back(std::move(back));
    rows.push_back(separator());

    HeaderMenuItem recolour;
    recolour.label      = _L("Change colour…");
    recolour.keeps_open = true;
    recolour.invoke     = [this, spool] {
        wxColourData data;
        data.SetChooseFull(true);
        data.SetColour(wxColour(wxString::FromUTF8(spool.colour)));
        wxColourDialog dialog(m_menu, &data);
        dialog.CenterOnParent();
        if (dialog.ShowModal() != wxID_OK) { show_spools(); return; }
        const wxColour picked = dialog.GetColourData().GetColour();
        m_host.store->recolour(spool.id, picked.GetAsString(wxC2S_HTML_SYNTAX).ToStdString());
        // Recolouring the spool the project is on must move the project too,
        // or the chip would show a colour the print will not use.
        if (spool.filament_preset == SetupCommands::current_filament().preset_name &&
            spool.colour == SetupCommands::current_colour().ToStdString())
            SetupCommands::set_filament_colour(*m_host.plater, picked);
        if (m_host.store_changed) m_host.store_changed();
        show_spools();
    };
    rows.push_back(std::move(recolour));

    HeaderMenuItem rename;
    rename.label      = _L("Rename…");
    rename.keeps_open = true;
    rename.invoke     = [this, spool] {
        wxTextEntryDialog dialog(m_menu, _L("Spool name"), _L("Rename spool"), wxString::FromUTF8(spool.name));
        if (dialog.ShowModal() == wxID_OK && !dialog.GetValue().Trim().empty()) {
            m_host.store->rename(spool.id, dialog.GetValue().Trim().ToStdString());
            if (m_host.store_changed) m_host.store_changed();
        }
        show_spools();
    };
    rows.push_back(std::move(rename));
    rows.push_back(separator());

    // Deferred in this slice, rendered so the shape of the menu is honest
    // about what is coming rather than growing later.
    HeaderMenuItem tune;
    tune.label   = _L("Tune with the agent");
    tune.enabled = false;
    rows.push_back(std::move(tune));

    HeaderMenuItem reset;
    reset.label   = _L("Reset tuning");
    reset.enabled = false;
    rows.push_back(std::move(reset));
    rows.push_back(separator());

    HeaderMenuItem settings;
    settings.label  = _L("Filament settings…");
    settings.invoke = [] { SetupCommands::open_settings_tab(Preset::TYPE_FILAMENT); };
    rows.push_back(std::move(settings));

    HeaderMenuItem remove;
    remove.label            = _L("Remove from this printer");
    remove.decoration.danger = true;
    remove.keeps_open       = true;
    remove.invoke           = [this, spool] {
        // Removing the spool forgets a note about a physical reel; it changes
        // nothing about the project, which keeps the preset and colour it has.
        m_host.store->remove(spool.id);
        if (m_host.store_changed) m_host.store_changed();
        show_spools();
    };
    rows.push_back(std::move(remove));

    m_menu->replace_items(std::move(rows));
}

void SpoolMenu::show_other_spool()
{
    const auto printer = SetupCommands::current_printer();

    // The search field lives above the rows and keeps focus while the result
    // list rebuilds under it on every keystroke.
    m_menu->set_header_builder([this](wxWindow* parent) -> wxWindow* {
        const auto& palette = m_theme.palette(m_dark);
        auto* field = new wxTextCtrl(parent, wxID_ANY, m_search, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
        field->SetHint(_L("Search filaments"));
        field->SetFont(Label::Body_14);
        field->SetBackgroundColour(palette.surface_subtle);
        field->SetForegroundColour(palette.text_primary);
        field->SetMinSize(wxSize(-1, field->FromDIP(28)));
        field->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
            m_search = event.GetString();
            // Rebuilding destroys this control, so leave the current event
            // first and let the rebuild re-create the field with the text.
            // The popup owns the deferral: if it dismisses in between, the
            // weak reference drops the call and this controller is gone too.
            m_menu->CallAfter([this, menu = wxWeakRef<wxWindow>(m_menu)] {
                if (menu) show_other_spool();
            });
        });
        m_search_field = field;
        return field;
    });

    std::vector<HeaderMenuItem> rows;
    rows.push_back(title_row(wxString::Format(_L("OTHER SPOOL · FITS %s · %g MM"),
                                              printer.nickname.Upper(), printer.nozzle)));

    for (const auto& filament : SetupCommands::compatible_filaments()) {
        if (!matches(filament, m_search)) continue;
        HeaderMenuItem row;
        row.label = filament.vendor.empty() ? filament.alias : filament.alias + middot() + filament.vendor;
        // A dashed ring: this preset is not a spool yet, and has no colour.
        row.decoration.dot = wxColour();
        row.keeps_open     = true;
        row.invoke         = [this, filament] { show_new_spool(filament); };
        rows.push_back(std::move(row));
    }

    HeaderMenuItem generic;
    generic.separator = true;
    generic.label     = _L("Generic preset…");
    generic.keeps_open = true;
    generic.invoke    = [this] {
        // Generic presets are ordinary compatible presets from the "Generic"
        // vendor, so this is the same list filtered, not a separate source.
        m_search = "Generic";
        show_other_spool();
    };
    rows.push_back(std::move(generic));

    HeaderMenuItem import;
    import.label  = _L("Import a preset file…");
    import.invoke = [] { SetupCommands::import_preset_file(); };
    rows.push_back(std::move(import));

    m_menu->replace_items(std::move(rows));
    if (m_search_field) m_search_field->SetFocus();
}

void SpoolMenu::show_new_spool(const SetupCommands::FilamentInfo& preset)
{
    const auto printer = SetupCommands::current_printer();
    const bool first   = m_new_preset != preset.preset_name;
    if (first) {
        m_new_preset      = preset.preset_name;
        m_new_material    = preset.material.empty() ? preset.alias : preset.material;
        m_new_vendor      = preset.vendor;
        m_new_colour      = wxColour(wxString::FromUTF8(SetupCommands::swatches().front().hex));
        m_new_name_edited = false;
    }
    if (!m_new_name_edited) {
        const wxString word = SetupCommands::colour_word(m_new_colour);
        m_new_name = word.empty() ? preset.alias : word + " " + m_new_material;
    }

    m_menu->set_header_builder([this, preset](wxWindow* parent) -> wxWindow* {
        const auto& palette = m_theme.palette(m_dark);
        auto* panel = new wxPanel(parent);
        panel->SetBackgroundColour(palette.surface_raised);
        auto* column = new wxBoxSizer(wxVERTICAL);

        auto caption = [&](const wxString& text, const wxFont& font, const wxColour& colour) {
            auto* label = new wxStaticText(panel, wxID_ANY, text);
            label->SetFont(font);
            label->SetForegroundColour(colour);
            return label;
        };
        column->Add(caption(preset.alias, Label::Body_14, palette.text_primary), 0, wxBOTTOM, panel->FromDIP(8));
        column->Add(caption(_L("COLOUR"), Label::Body_10, palette.text_secondary), 0, wxBOTTOM, panel->FromDIP(4));

        // Twelve swatches on two rows. Each is a small owner-drawn panel
        // rather than a button: it carries one value and one selected ring.
        auto* grid = new wxGridSizer(kSwatchesPerRow, panel->FromDIP(4), panel->FromDIP(4));
        for (const auto& swatch : SetupCommands::swatches()) {
            const wxColour colour(wxString::FromUTF8(swatch.hex));
            auto* cell = new wxPanel(panel, wxID_ANY, wxDefaultPosition, panel->FromDIP(wxSize(24, 24)));
            cell->SetName(swatch.name);
            cell->SetToolTip(_(swatch.name));
            cell->SetBackgroundStyle(wxBG_STYLE_PAINT);
            cell->Bind(wxEVT_PAINT, [this, cell, colour, palette](wxPaintEvent&) {
                wxPaintDC dc(cell);
                dc.SetBackground(wxBrush(palette.surface_raised));
                dc.Clear();
                const bool chosen = m_new_colour == colour;
                dc.SetBrush(wxBrush(colour));
                dc.SetPen(wxPen(chosen ? palette.border_strong : palette.border_subtle, chosen ? 2 : 1));
                const wxRect box = cell->GetClientRect().Deflate(chosen ? 1 : 2);
                dc.DrawRoundedRectangle(box, cell->FromDIP(4));
            });
            cell->Bind(wxEVT_LEFT_UP, [this, colour, preset](wxMouseEvent&) {
                m_new_colour = colour;
                m_menu->CallAfter([this, preset, menu = wxWeakRef<wxWindow>(m_menu)] {
                    if (menu) show_new_spool(preset);
                });
            });
            grid->Add(cell, 0);
        }
        column->Add(grid, 0, wxBOTTOM, panel->FromDIP(8));

        auto* name = new wxTextCtrl(panel, wxID_ANY, m_new_name);
        name->SetFont(Label::Body_14);
        name->SetBackgroundColour(palette.surface_subtle);
        name->SetForegroundColour(palette.text_primary);
        name->SetMinSize(wxSize(-1, panel->FromDIP(28)));
        name->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
            // Once the person types, the colour no longer rewrites the name.
            m_new_name        = event.GetString();
            m_new_name_edited = true;
        });
        m_name_field = name;
        column->Add(name, 0, wxEXPAND);

        panel->SetSizerAndFit(column);
        return panel;
    });

    std::vector<HeaderMenuItem> rows;
    HeaderMenuItem back;
    back.label      = wxString::Format(_L("NEW SPOOL FOR %s"), printer.nickname.Upper());
    back.icon       = HeaderIcon::Back;
    back.keeps_open = true;
    back.invoke     = [this] { m_new_preset.clear(); show_other_spool(); };
    rows.push_back(std::move(back));

    HeaderMenuItem use;
    use.label                = _L("Use this spool");
    use.separator            = true;
    use.decoration.detail    = _L("adds it to the list");
    use.decoration.bold      = true;
    use.invoke               = [this, printer] {
        Workspace::Spool spool;
        spool.printer_preset  = printer.preset_name;
        spool.filament_preset = m_new_preset;
        spool.colour          = m_new_colour.GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
        spool.name            = m_new_name.Trim().ToStdString();
        spool.brand           = m_new_vendor.ToStdString();
        if (spool.name.empty()) spool.name = m_new_material.ToStdString();
        select(m_host.store->add(std::move(spool)));
    };
    rows.push_back(std::move(use));

    m_menu->replace_items(std::move(rows));
    if (m_name_field && !m_new_name_edited) m_name_field->SetInsertionPointEnd();
}

} // namespace Slic3r::GUI::JusPrin
