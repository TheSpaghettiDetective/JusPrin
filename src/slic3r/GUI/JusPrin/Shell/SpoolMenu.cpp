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
    auto self = std::make_shared<SpoolMenu>(owner, theme, dark, std::move(host));
    self->m_anchor = &anchor;
    self->m_menu = new HeaderMenu(owner, theme, dark, {});
    show_spools(self);
    self->m_menu->open(anchor);
    // No dismiss listener: the row callbacks own the controller, so it is
    // released when the popup's rows are destroyed and any queued callback has
    // run -- never while one is still pending.
}

void SpoolMenu::select(const Ptr& self, const Workspace::Spool& spool)
{
    Host& host = self->m_host;
    // Order matters: the preset switch first, because it can pull a colour of
    // its own from the preset, then the spool's colour over the top, then the
    // recency stamp. The chip refreshes from Orca's own settings-changed
    // notification, not from here.
    if (!SetupCommands::select_filament_preset(*host.plater, spool.filament_preset))
        return;
    const wxColour colour(wxString::FromUTF8(spool.colour));
    if (colour.IsOk())
        SetupCommands::set_filament_colour(*host.plater, colour);
    host.store->touch(spool.id);
    if (host.selected) host.selected(spool);
}

// Brings the spool list back after a step that had to close the popup.
void SpoolMenu::reopen(const Ptr& self)
{
    if (!self->m_anchor) return;
    open(self->m_owner, self->m_theme, self->m_dark, self->m_host, *self->m_anchor);
}

void SpoolMenu::show_spools(const Ptr& self)
{
    auto* menu = self->m_menu.get();
    if (menu == nullptr) return; // the popup went away; nothing to rebuild
    menu->set_header_builder(nullptr);
    const auto printer = SetupCommands::current_printer();
    const auto current = self->m_host.store->current(printer.preset_name,
                                               SetupCommands::current_filament().preset_name,
                                               SetupCommands::current_colour().ToStdString());

    std::vector<HeaderMenuItem> rows;
    rows.push_back(title_row(wxString::Format(_L("SPOOLS ON %s"), printer.nickname.Upper())));

    // Never truncated and never scrolled: a person keeps three to fifteen
    // spools, and the whole design rests on the right one being one click away.
    for (const Workspace::Spool& spool : self->m_host.store->spools_for(printer.preset_name)) {
        HeaderMenuItem row;
        row.label            = spool_label(spool);
        row.decoration.dot   = wxColour(wxString::FromUTF8(spool.colour));
        row.decoration.check = current.has_value() && current->id == spool.id;
        row.invoke           = [self, spool] { SpoolMenu::select(self, spool); };
        row.row_action       = HeaderIcon::More;
        row.invoke_row_action = [self, spool] { show_row_menu(self, spool); };
        rows.push_back(std::move(row));
    }

    HeaderMenuItem other;
    other.separator          = true;
    other.label              = _L("Other spool…");
    other.decoration.trailing = HeaderIcon::Right;
    other.keeps_open         = true;
    other.invoke             = [self] { show_other_spool(self); };
    rows.push_back(std::move(other));

    menu->replace_items(std::move(rows));
}

void SpoolMenu::show_row_menu(const Ptr& self, const Workspace::Spool& spool)
{
    auto* menu = self->m_menu.get();
    if (menu == nullptr) return; // the popup went away; nothing to rebuild
    menu->set_header_builder(nullptr);
    std::vector<HeaderMenuItem> rows;

    HeaderMenuItem back;
    back.label      = wxString::FromUTF8(spool.name);
    back.icon       = HeaderIcon::Back;
    back.keeps_open = true;
    back.invoke     = [self] { show_spools(self); };
    rows.push_back(std::move(back));
    rows.push_back(separator());

    HeaderMenuItem recolour;
    recolour.label      = _L("Change colour…");
    // NOT keeps_open. The system colour panel has no accept or cancel button
    // of its own on macOS, so its window frame is the only way out -- and a
    // transient popup renders above it and covers that frame. The popup has to
    // be gone before the dialog appears, or the person is trapped.
    recolour.invoke     = [self, spool] {
        wxColourData data;
        data.SetChooseFull(true);
        data.SetColour(wxColour(wxString::FromUTF8(spool.colour)));
        wxColourDialog dialog(self->m_owner, &data);
        dialog.CenterOnParent();
        if (dialog.ShowModal() != wxID_OK) { reopen(self); return; }
        const wxColour picked = dialog.GetColourData().GetColour();
        self->m_host.store->recolour(spool.id, picked.GetAsString(wxC2S_HTML_SYNTAX).ToStdString());
        // Recolouring the spool the project is on must move the project too,
        // or the chip would show a colour the print will not use.
        if (spool.filament_preset == SetupCommands::current_filament().preset_name &&
            spool.colour == SetupCommands::current_colour().ToStdString())
            SetupCommands::set_filament_colour(*self->m_host.plater, picked);
        if (self->m_host.store_changed) self->m_host.store_changed();
        reopen(self);
    };
    rows.push_back(std::move(recolour));

    HeaderMenuItem rename;
    rename.label      = _L("Rename…");
    rename.invoke     = [self, spool] {
        wxTextEntryDialog dialog(self->m_owner, _L("Spool name"), _L("Rename spool"), wxString::FromUTF8(spool.name));
        if (dialog.ShowModal() == wxID_OK && !dialog.GetValue().Trim().empty()) {
            self->m_host.store->rename(spool.id, dialog.GetValue().Trim().ToStdString());
            if (self->m_host.store_changed) self->m_host.store_changed();
        }
        reopen(self);
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
    remove.invoke           = [self, spool] {
        // Removing the spool forgets a note about a physical reel; it changes
        // nothing about the project, which keeps the preset and colour it has.
        self->m_host.store->remove(spool.id);
        if (self->m_host.store_changed) self->m_host.store_changed();
        show_spools(self);
    };
    rows.push_back(std::move(remove));

    menu->replace_items(std::move(rows));
}

void SpoolMenu::show_other_spool(const Ptr& self)
{
    auto* menu = self->m_menu.get();
    if (menu == nullptr) return; // the popup went away; nothing to rebuild
    const auto printer = SetupCommands::current_printer();

    // The search field lives above the rows and keeps focus while the result
    // list rebuilds under it on every keystroke.
    menu->set_header_builder([self](wxWindow* parent) -> wxWindow* {
        const auto& palette = self->m_theme.palette(self->m_dark);
        auto* field = new wxTextCtrl(parent, wxID_ANY, self->m_search, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
        field->SetHint(_L("Search filaments"));
        field->SetFont(Label::Body_14);
        field->SetBackgroundColour(palette.surface_subtle);
        field->SetForegroundColour(palette.text_primary);
        field->SetMinSize(wxSize(-1, field->FromDIP(28)));
        field->Bind(wxEVT_TEXT, [self](wxCommandEvent& event) {
            self->m_search = event.GetString();
            // Rebuilding destroys this control, so leave the current event
            // first and let the rebuild re-create the field with the text.
            // The popup owns the deferral: if it dismisses in between, the
            // weak reference drops the call and this controller is gone too.
            self->m_owner->CallAfter([self, alive = wxWeakRef<wxWindow>(self->m_menu)] {
                if (alive) show_other_spool(self);
            });
        });
        self->m_search_field = field;
        return field;
    }, 1); // below the back row, as the design places it

    std::vector<HeaderMenuItem> rows;
    // The title is also the way back to the spool list; without it the only
    // exit from this step is dismissing the whole menu.
    HeaderMenuItem back;
    back.label      = wxString::Format(_L("OTHER SPOOL · FITS %s · %g MM"), printer.nickname.Upper(), printer.nozzle);
    back.icon       = HeaderIcon::Back;
    back.keeps_open = true;
    back.invoke     = [self] { self->m_search.clear(); show_spools(self); };
    rows.push_back(std::move(back));

    for (const auto& filament : SetupCommands::compatible_filaments()) {
        if (!matches(filament, self->m_search)) continue;
        HeaderMenuItem row;
        row.label = filament.vendor.empty() ? filament.alias : filament.alias + middot() + filament.vendor;
        // A dashed ring: this preset is not a spool yet, and has no colour.
        row.decoration.dot = wxColour();
        row.keeps_open     = true;
        row.invoke         = [self, filament] { show_new_spool(self, filament); };
        rows.push_back(std::move(row));
    }

    HeaderMenuItem generic;
    generic.separator = true;
    generic.label     = _L("Generic preset…");
    generic.keeps_open = true;
    generic.invoke    = [self] {
        // Generic presets are ordinary compatible presets from the "Generic"
        // vendor, so this is the same list filtered, not a separate source.
        self->m_search = "Generic";
        show_other_spool(self);
    };
    rows.push_back(std::move(generic));

    HeaderMenuItem import;
    import.label  = _L("Import a preset file…");
    import.invoke = [] { SetupCommands::import_preset_file(); };
    rows.push_back(std::move(import));

    menu->replace_items(std::move(rows));
    // The rebuild replaced the field, so focus and caret have to be restored
    // or every keystroke would land at the start of what was already typed.
    if (self->m_search_field) {
        self->m_search_field->SetFocus();
        self->m_search_field->SetInsertionPointEnd();
    }
}

void SpoolMenu::show_new_spool(const Ptr& self, const SetupCommands::FilamentInfo& preset)
{
    auto* menu = self->m_menu.get();
    if (menu == nullptr) return; // the popup went away; nothing to rebuild
    const auto printer = SetupCommands::current_printer();
    const bool first   = self->m_new_preset != preset.preset_name;
    if (first) {
        self->m_new_preset      = preset.preset_name;
        self->m_new_material    = preset.material.empty() ? preset.alias : preset.material;
        self->m_new_vendor      = preset.vendor;
        self->m_new_colour      = wxColour(wxString::FromUTF8(SetupCommands::swatches().front().hex));
        self->m_new_name_edited = false;
    }
    if (!self->m_new_name_edited) {
        const wxString word = SetupCommands::colour_word(self->m_new_colour);
        self->m_new_name = word.empty() ? preset.alias : word + " " + self->m_new_material;
    }

    menu->set_header_builder([self, preset](wxWindow* parent) -> wxWindow* {
        const auto& palette = self->m_theme.palette(self->m_dark);
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
            cell->Bind(wxEVT_PAINT, [self, cell, colour, palette](wxPaintEvent&) {
                wxPaintDC dc(cell);
                dc.SetBackground(wxBrush(palette.surface_raised));
                dc.Clear();
                const bool chosen = self->m_new_colour == colour;
                dc.SetBrush(wxBrush(colour));
                dc.SetPen(wxPen(chosen ? palette.border_strong : palette.border_subtle, chosen ? 2 : 1));
                const wxRect box = cell->GetClientRect().Deflate(chosen ? 1 : 2);
                dc.DrawRoundedRectangle(box, cell->FromDIP(6)); // standard control radius
            });
            cell->Bind(wxEVT_LEFT_UP, [self, colour, preset](wxMouseEvent&) {
                self->m_new_colour = colour;
                self->m_owner->CallAfter([self, preset, alive = wxWeakRef<wxWindow>(self->m_menu)] {
                    if (alive) show_new_spool(self, preset);
                });
            });
            grid->Add(cell, 0);
        }
        column->Add(grid, 0, wxBOTTOM, panel->FromDIP(8));

        auto* name = new wxTextCtrl(panel, wxID_ANY, self->m_new_name);
        name->SetFont(Label::Body_14);
        name->SetBackgroundColour(palette.surface_subtle);
        name->SetForegroundColour(palette.text_primary);
        name->SetMinSize(wxSize(-1, panel->FromDIP(28)));
        name->Bind(wxEVT_TEXT, [self](wxCommandEvent& event) {
            // Once the person types, the colour no longer rewrites the name.
            self->m_new_name        = event.GetString();
            self->m_new_name_edited = true;
        });
        self->m_name_field = name;
        column->Add(name, 0, wxEXPAND);

        panel->SetSizerAndFit(column);
        return panel;
    }, 1); // below the back row

    std::vector<HeaderMenuItem> rows;
    HeaderMenuItem back;
    back.label      = wxString::Format(_L("NEW SPOOL FOR %s"), printer.nickname.Upper());
    back.icon       = HeaderIcon::Back;
    back.keeps_open = true;
    back.invoke     = [self] { self->m_new_preset.clear(); show_other_spool(self); };
    rows.push_back(std::move(back));

    HeaderMenuItem use;
    use.label                = _L("Use this spool");
    use.separator            = true;
    use.decoration.detail    = _L("adds it to the list");
    use.decoration.bold      = true;
    use.invoke               = [self, printer] {
        Workspace::Spool spool;
        spool.printer_preset  = printer.preset_name;
        spool.filament_preset = self->m_new_preset;
        spool.colour          = self->m_new_colour.GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
        spool.name            = self->m_new_name.Trim().ToStdString();
        spool.brand           = self->m_new_vendor.ToStdString();
        if (spool.name.empty()) spool.name = self->m_new_material.ToStdString();
        SpoolMenu::select(self, self->m_host.store->add(std::move(spool)));
    };
    rows.push_back(std::move(use));

    menu->replace_items(std::move(rows));
    if (self->m_name_field && !self->m_new_name_edited) self->m_name_field->SetInsertionPointEnd();
}

} // namespace Slic3r::GUI::JusPrin
