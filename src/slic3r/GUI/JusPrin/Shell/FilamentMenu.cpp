#include "FilamentMenu.hpp"
#include "ShellRecipes.hpp"
#include "ShellController.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <wx/colordlg.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/weakref.h>

#include <algorithm>
#include <functional>

namespace Slic3r::GUI::JusPrin {

namespace {

// Two rows of the colour grid at most: the current colour, the saved ones,
// and the "+" that opens the system picker. The design's numbers.
constexpr std::size_t kColoursPerRow = 8;
constexpr std::size_t kColourCells   = 2 * kColoursPerRow;

// The colour row's name for its "+" cell, where a swatch cell uses its value.
constexpr const char* kPickerKey = "+";

wxString middot() { return wxString::FromUTF8(" \xC2\xB7 "); }

wxString nozzle_text(double nozzle) { return wxString::Format("%g", nozzle); }

// "PRUSA MK4 · 0.4", or the name alone on a printer whose nozzles differ,
// where one number would be wrong for some slots.
wxString printer_caption(const SetupCommands::PrinterInfo& printer, double nozzle)
{
    wxString caption = printer.nickname.Upper();
    if (nozzle > 0.)
        caption += middot() + nozzle_text(nozzle);
    return caption;
}

double shared_nozzle(const SetupCommands::PrinterInfo& printer)
{
    const auto& nozzles = printer.nozzles;
    const bool  mixed   = std::adjacent_find(nozzles.begin(), nozzles.end(), std::not_equal_to<double>()) != nozzles.end();
    return mixed ? 0. : printer.nozzle;
}

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

std::string colour_key(const wxColour& colour)
{
    return colour.IsOk() ? std::string(colour.GetAsString(wxC2S_HTML_SYNTAX).ToUTF8()) : std::string(kPickerKey);
}

// One colour cell of the grid: a swatch, or the "+" that opens the picker.
// It only draws and reports clicks; the row owns what a cell does.
class ColourCell : public wxPanel
{
public:
    ColourCell(wxWindow* parent, const ShellTheme& theme, const ShellPalette& palette, const wxColour& colour,
               bool current, std::function<void()> clicked)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition,
                  parent->FromDIP(wxSize(theme.metrics().swatch.size, theme.metrics().swatch.size)))
        , m_swatch(theme.metrics().swatch), m_palette(palette), m_colour(colour), m_current(current)
        , m_clicked(std::move(clicked))
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetCursor(wxCursor(wxCURSOR_HAND));
        Bind(wxEVT_PAINT, &ColourCell::paint, this);
        Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { m_hover = true; Refresh(); });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { m_hover = false; Refresh(); });
        Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) { if (m_clicked) m_clicked(); });
    }

private:
    void paint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(m_palette.surface_raised));
        dc.Clear();
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (!gc) return;
        const wxRect box    = GetClientRect().Deflate(FromDIP(m_current ? m_swatch.current_inset : m_swatch.inset));
        const double radius = FromDIP(m_swatch.radius);
        if (!m_colour.IsOk()) {
            // The "+": the system picker, for a colour not saved yet.
            gc->SetPen(wxPen(m_hover ? m_palette.border_strong : m_palette.border_subtle, FromDIP(m_swatch.ring_width)));
            gc->SetBrush(*wxTRANSPARENT_BRUSH);
            gc->DrawRoundedRectangle(box.x + 0.5, box.y + 0.5, box.width - 1, box.height - 1, radius);
            gc->SetPen(wxPen(m_palette.text_secondary, FromDIP(m_swatch.glyph_width)));
            const double cx = box.x + box.width / 2., cy = box.y + box.height / 2., arm = box.width / 4.;
            gc->StrokeLine(cx - arm, cy, cx + arm, cy);
            gc->StrokeLine(cx, cy - arm, cx, cy + arm);
            return;
        }
        // The current colour is ringed; the others show a quiet edge so a
        // white or near-background colour stays visible.
        gc->SetPen(wxPen(m_current || m_hover ? m_palette.border_strong : m_palette.border_subtle,
                         FromDIP(m_current ? m_swatch.current_ring_width : m_swatch.ring_width)));
        gc->SetBrush(wxBrush(m_colour));
        gc->DrawRoundedRectangle(box.x + 0.5, box.y + 0.5, box.width - 1, box.height - 1, radius);
    }

    SwatchMetrics         m_swatch;
    ShellPalette          m_palette;
    wxColour              m_colour;
    bool                  m_current{false};
    bool                  m_hover{false};
    std::function<void()> m_clicked;
};

// The colour row: its caption, then the grid of cells. It is one stop in the
// menu's keyboard model: Left and Right move between cells, Return and Space
// apply one, and the cell the keyboard is on carries the focus ring, drawn in
// the gap around it so the swatch itself never changes size.
class ColourRow : public wxPanel, public HeaderMenuKeyView
{
public:
    struct Cell
    {
        wxColour              colour; // invalid for the "+"
        bool                  current{false};
        std::function<void()> apply;
    };

    ColourRow(wxWindow* parent, const ShellTheme& theme, const ShellPalette& palette, std::vector<Cell> cells,
              const std::string& cursor, std::function<void(const std::string&)> cursor_moved)
        : wxPanel(parent)
        , m_palette(palette), m_swatch(theme.metrics().swatch), m_ring(theme.metrics().focus.width)
        , m_cursor_moved(std::move(cursor_moved))
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &ColourRow::paint, this);
        auto* column = new wxBoxSizer(wxVERTICAL);

        auto* caption = new wxStaticText(this, wxID_ANY, _L("COLOUR"));
        style_label(*caption, theme, TextRole::Metadata, palette.text_secondary);
        caption->SetBackgroundColour(palette.surface_raised);
        // The caption lines up with the cells, which stand in by the ring's
        // width so a ring round an outer cell is not cut off.
        column->Add(caption, 0, wxLEFT | wxBOTTOM, FromDIP(m_ring));
        column->AddSpacer(FromDIP(m_swatch.caption_gap) - FromDIP(m_ring));

        auto* grid = new wxGridSizer(int(kColoursPerRow), FromDIP(m_swatch.gap), FromDIP(m_swatch.gap));
        for (std::size_t index = 0; index < cells.size(); ++index) {
            auto* cell = new ColourCell(this, theme, palette, cells[index].colour, cells[index].current,
                                        [this, index] { activate(index); });
            const std::string key = colour_key(cells[index].colour);
            cell->SetName(key == kPickerKey ? wxString("Other colour") : wxString::FromUTF8(key));
            cell->SetToolTip(key == kPickerKey ? _L("Other colour…") : wxString::FromUTF8(key));
            grid->Add(cell, 0);
            m_cells.push_back(cell);
            m_keys.push_back(key);
            m_apply.push_back(std::move(cells[index].apply));
        }
        column->Add(grid, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(m_ring));
        SetSizerAndFit(column);

        const auto found = std::find(m_keys.begin(), m_keys.end(), cursor);
        m_cursor         = found == m_keys.end() ? 0 : std::size_t(found - m_keys.begin());
    }

    void set_key_focus(bool focused) override
    {
        m_focused = focused;
        if (focused) report_cursor();
        Refresh();
    }

    bool on_menu_key(int key_code) override
    {
        if (m_cells.empty()) return false;
        switch (key_code) {
        case WXK_LEFT:
            if (m_cursor > 0) --m_cursor;
            report_cursor();
            Refresh();
            return true;
        case WXK_RIGHT:
            if (m_cursor + 1 < m_cells.size()) ++m_cursor;
            report_cursor();
            Refresh();
            return true;
        case WXK_RETURN:
        case WXK_NUMPAD_ENTER:
        case WXK_SPACE:
            activate(m_cursor);
            return true;
        default: return false;
        }
    }

    wxWindow* key_child() const override { return m_cells.empty() ? nullptr : m_cells[m_cursor]; }

private:
    void report_cursor()
    {
        if (m_cursor_moved && m_cursor < m_keys.size()) m_cursor_moved(m_keys[m_cursor]);
    }

    // Applying a colour rebuilds the menu, which destroys this row and the
    // cell that asked. The menu runs it once the current event is done.
    void activate(std::size_t index)
    {
        if (index >= m_apply.size()) return;
        m_cursor = index;
        report_cursor();
        auto apply = m_apply[index];
        wxWindow* menu = GetParent();
        menu->CallAfter([alive = wxWeakRef<wxWindow>(menu), apply] { if (alive && apply) apply(); });
    }

    void paint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(m_palette.surface_raised));
        dc.Clear();
        if (!m_focused || m_cursor >= m_cells.size()) return;
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (!gc) return;
        // The ring sits wholly outside the cell, in the gap the grid leaves.
        const double ring = FromDIP(m_ring);
        const wxRect cell = m_cells[m_cursor]->GetRect();
        gc->SetPen(wxPen(m_palette.border_focus, ring));
        gc->SetBrush(*wxTRANSPARENT_BRUSH);
        gc->DrawRoundedRectangle(cell.x - ring / 2., cell.y - ring / 2., cell.width + ring, cell.height + ring,
                                 FromDIP(m_swatch.radius) + ring / 2.);
    }

    ShellPalette                             m_palette;
    SwatchMetrics                            m_swatch;
    int                                      m_ring{0};
    std::function<void(const std::string&)> m_cursor_moved;
    std::vector<ColourCell*>                 m_cells;
    std::vector<std::string>                 m_keys;
    std::vector<std::function<void()>>       m_apply;
    std::size_t                              m_cursor{0};
    bool                                     m_focused{false};
};

} // namespace

FilamentMenu::FilamentMenu(wxWindow* owner, const ShellTheme& theme, bool dark, Host host)
    : m_owner(owner), m_theme(theme), m_dark(dark), m_host(std::move(host))
{}

void FilamentMenu::open(wxWindow* owner, const ShellTheme& theme, bool dark, Host host, HeaderButton& anchor)
{
    auto self      = std::make_shared<FilamentMenu>(owner, theme, dark, std::move(host));
    self->m_anchor = &anchor;
    reopen(self, View{});
}

void FilamentMenu::reopen(const Ptr& self, View view)
{
    self->m_stepping_out = false;
    if (!self->m_anchor) {
        // The menu cannot come back, so the visit ends here and its line is
        // not lost.
        end_visit(self);
        return;
    }
    auto* menu   = new HeaderMenu(self->m_owner, self->m_theme, self->m_dark, {});
    self->m_menu = menu;
    // A visit ends when the person leaves the menu: click-away, Esc, Tab, or a
    // row that hands off for good (Filament settings…, the tray row). A close
    // the menu makes itself, to put a dialog up and come back, is not one.
    // The callbacks hold the controller, so it is still alive here.
    menu->set_dismiss_listener([self] {
        if (self->m_stepping_out) return;
        self->m_owner->CallAfter([self] { end_visit(self); });
    });
    show(self, view);
    menu->open(*self->m_anchor);
}

void FilamentMenu::step_out(const Ptr& self, View back, const std::function<void()>& task)
{
    // The dialogs this puts up are modal, and on macOS a system panel's window
    // frame is its only way out -- a transient popup above it would cover
    // that frame. The popup goes first and comes back afterwards, in the same
    // visit.
    self->m_stepping_out = true;
    if (auto* menu = self->m_menu.get()) menu->close();
    task();
    reopen(self, back);
}

void FilamentMenu::end_visit(const Ptr& self)
{
    if (self->m_visit.filaments.empty()) return;
    const Visit visit = std::move(self->m_visit);
    self->m_visit     = {};
    if (self->m_host.visit_ended) self->m_host.visit_ended(visit);
}

void FilamentMenu::show(const Ptr& self, View view)
{
    const auto slots = SetupCommands::filament_slots();
    if (view.slot && *view.slot < slots.size())
        show_slot(self, *view.slot, view.from_list);
    else if (slots.size() > 1)
        show_slots(self);
    else
        show_slot(self, 0, false);
}

void FilamentMenu::add_tray_row(const Ptr& self, std::vector<HeaderMenuItem>& rows)
{
    const SetupCommands::TrayState trays = SetupCommands::printer_trays();
    if (trays == SetupCommands::TrayState::None)
        return; // no trays reported: no row at all
    HeaderMenuItem row;
    row.label = _L("Use what's loaded on the printer…");
    if (trays == SetupCommands::TrayState::Offline) {
        // Kept in place, greyed, with the reason, so the menu does not jump
        // when the printer comes back.
        row.enabled              = false;
        row.decoration.sub_label = wxString::Format(_L("%s is offline. Turn it on to read what's loaded."),
                                                    SetupCommands::current_printer().nickname);
    } else {
        // On request only: Orca's own sync, unchanged. It hands off for good,
        // so the menu closes and the visit ends.
        row.invoke = [self] {
            if (self->m_host.plater && SetupCommands::sync_from_printer(*self->m_host.plater) && self->m_host.changed)
                self->m_host.changed();
        };
    }
    rows.push_back(std::move(row));
    rows.push_back(separator());
}

void FilamentMenu::show_slots(const Ptr& self)
{
    auto* menu = self->m_menu.get();
    if (menu == nullptr) return; // the popup went away; nothing to rebuild
    menu->set_header_builder(nullptr);
    const auto printer = SetupCommands::current_printer();
    const auto slots   = SetupCommands::filament_slots();

    std::vector<HeaderMenuItem> rows;
    add_tray_row(self, rows);
    rows.push_back(title_row(wxString::Format(_L("FILAMENTS ON %s"), printer_caption(printer, shared_nozzle(printer)))));
    for (const auto& slot : slots) {
        HeaderMenuItem row;
        // Number, dot, name, as the design reads. The number is drawn ahead
        // of the dot; the row's name still says all three in words.
        row.label           = slot.filament.alias;
        row.name            = wxString::Format("%d  %s", int(slot.index + 1), slot.filament.alias);
        row.decoration.lead = wxString::Format("%d", int(slot.index + 1));
        row.decoration.dot  = wxColour(slot.colour);
        wxString detail;
        if (slot.nozzle > 0.)
            detail = nozzle_text(slot.nozzle);
        if (slot.used)
            detail += (detail.empty() ? wxString() : middot()) + _L("on plate");
        row.decoration.detail   = detail;
        row.decoration.trailing = HeaderIcon::Right;
        row.keeps_open          = true;
        row.invoke              = [self, index = slot.index] { show_slot(self, index, true); };
        rows.push_back(std::move(row));
    }

    HeaderMenuItem add;
    add.separator  = true;
    add.label      = _L("Add a slot");
    add.keeps_open = true;
    add.invoke     = [self] {
        if (!self->m_host.plater) return;
        const auto added = SetupCommands::add_filament_slot(*self->m_host.plater);
        if (self->m_host.changed) self->m_host.changed();
        show(self, View{added, true});
    };
    rows.push_back(std::move(add));
    menu->replace_items(std::move(rows));
}

void FilamentMenu::show_slot(const Ptr& self, std::size_t slot, bool from_list)
{
    auto* menu = self->m_menu.get();
    if (menu == nullptr) return; // the popup went away; nothing to rebuild
    const auto printer = SetupCommands::current_printer();
    const auto slots   = SetupCommands::filament_slots();
    if (slot >= slots.size()) { show_slots(self); return; }
    const auto& current = slots[slot];
    const View  here{slot, from_list};

    std::vector<HeaderMenuItem> rows;
    if (from_list) {
        // The title is also the way back to the slot list.
        HeaderMenuItem back;
        back.label      = wxString::Format(_L("FILAMENT · SLOT %d · %s"), int(slot + 1),
                                           printer_caption(printer, current.nozzle > 0. ? current.nozzle : printer.nozzle));
        back.icon       = HeaderIcon::Back;
        back.keeps_open = true;
        back.invoke     = [self] { show_slots(self); };
        rows.push_back(std::move(back));
    } else {
        add_tray_row(self, rows);
        rows.push_back(title_row(wxString::Format(_L("FILAMENT ON %s"), printer_caption(printer, printer.nozzle))));
    }
    // The colour row sits directly under the title: colour is the most
    // frequent change. The popup counts rows it places, and a bare separator
    // is not one.
    const int colour_after = int(std::count_if(rows.begin(), rows.end(),
                                               [](const HeaderMenuItem& row) { return !(row.title && row.label.empty()); }));
    menu->set_header_builder([self, here](wxWindow* parent) { return build_colour_row(self, parent, here); }, colour_after);

    rows.push_back(title_row(_L("INSTALLED")));
    for (const auto& filament : SetupCommands::compatible_filaments()) {
        HeaderMenuItem row;
        row.label            = filament.alias;
        row.decoration.check = filament.preset_name == current.filament.preset_name;
        // Stays open: a colour click after this is the same visit.
        row.keeps_open = true;
        row.invoke     = [self, slot, from_list, preset = filament.preset_name] {
            if (!self->m_host.plater) return;
            if (SetupCommands::select_filament_preset(*self->m_host.plater, slot, preset)) {
                self->m_visit.filaments[slot] = preset;
                if (self->m_host.changed) self->m_host.changed();
            }
            show_slot(self, slot, from_list);
        };
        rows.push_back(std::move(row));
    }

    // Everything that fits, installed or not, is Orca's own page; it installs
    // what is ticked there, and the menu comes back with the list current,
    // in the same visit.
    HeaderMenuItem other;
    other.separator           = true;
    other.label               = _L("Other filament…");
    other.decoration.trailing = HeaderIcon::Right;
    other.keeps_open          = true;
    other.invoke              = [self, here] {
        step_out(self, here, [self] {
            SetupCommands::open_filament_library();
            if (self->m_host.changed) self->m_host.changed();
        });
    };
    rows.push_back(std::move(other));

    HeaderMenuItem settings;
    settings.label  = _L("Filament settings…");
    settings.invoke = [slot, name = current.filament.alias.ToStdString()] {
        if (ShellController* shell = installed_shell())
            shell->open_filament_help(slot, name);
    };
    rows.push_back(std::move(settings));

    // Adding a slot is offered on the one-slot menu, which then becomes the
    // slot list with the new slot open; a slot's own view does not offer it.
    if (!from_list) {
        HeaderMenuItem add;
        add.label      = _L("Add a slot");
        add.keeps_open = true;
        add.invoke     = [self] {
            if (!self->m_host.plater) return;
            const auto added = SetupCommands::add_filament_slot(*self->m_host.plater);
            if (self->m_host.changed) self->m_host.changed();
            show(self, View{added, true});
        };
        rows.push_back(std::move(add));
    }
    menu->replace_items(std::move(rows));
}

wxWindow* FilamentMenu::build_colour_row(const Ptr& self, wxWindow* parent, View view)
{
    const std::size_t slot    = view.slot.value_or(0);
    const auto&       palette = self->m_theme.palette(self->m_dark);
    const auto        slots   = SetupCommands::filament_slots();
    const wxColour    current = slot < slots.size() ? wxColour(slots[slot].colour) : wxColour();

    // The slot's own colour first, then the saved ones, each once. No names:
    // the app holds a colour as a value and nothing more.
    std::vector<wxColour> colours;
    if (current.IsOk()) colours.push_back(current);
    for (const wxColour& saved : SetupCommands::saved_colours())
        if (std::find(colours.begin(), colours.end(), saved) == colours.end())
            colours.push_back(saved);
    if (colours.size() > kColourCells - 1) colours.resize(kColourCells - 1);

    std::vector<ColourRow::Cell> cells;
    for (const wxColour& colour : colours)
        cells.push_back({colour, colour == current, [self, view, colour] {
                             // Colour only: the plan does not change, so the
                             // visit records nothing for the thread.
                             if (self->m_host.plater)
                                 SetupCommands::set_filament_colour(*self->m_host.plater, view.slot.value_or(0), colour);
                             if (self->m_host.changed) self->m_host.changed();
                             show(self, view);
                         }});
    cells.push_back({wxColour(), false, [self, view] { pick_colour(self, view.slot.value_or(0), view); }});

    // The keyboard's place is kept by value, so it stays on the same colour
    // when applying it moves that colour to the front of the row.
    return new ColourRow(parent, self->m_theme, palette, std::move(cells), self->m_colour_cursor,
                         [self](const std::string& key) { self->m_colour_cursor = key; });
}

void FilamentMenu::pick_colour(const Ptr& self, std::size_t slot, View back)
{
    step_out(self, back, [self, slot] {
        const auto   slots = SetupCommands::filament_slots();
        wxColourData data;
        data.SetChooseFull(true);
        if (slot < slots.size()) data.SetColour(wxColour(slots[slot].colour));
        const auto saved = SetupCommands::saved_colours();
        for (std::size_t i = 0; i < saved.size() && i < 16; ++i)
            data.SetCustomColour(int(i), saved[i]);
        wxColourDialog dialog(self->m_owner, &data);
        dialog.CenterOnParent();
        if (dialog.ShowModal() == wxID_OK) {
            const wxColour picked = dialog.GetColourData().GetColour();
            SetupCommands::remember_colour(picked);
            if (self->m_host.plater) SetupCommands::set_filament_colour(*self->m_host.plater, slot, picked);
            if (self->m_host.changed) self->m_host.changed();
        }
    });
}

} // namespace Slic3r::GUI::JusPrin
