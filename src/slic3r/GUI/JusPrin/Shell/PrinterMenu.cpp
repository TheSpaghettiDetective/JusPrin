#include "PrinterMenu.hpp"
#include "SetupCommands.hpp"
#include "ShellController.hpp"
#include "StatusRow.hpp"
#include "slic3r/GUI/JusPrin/Home/HomeWebView.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Plater.hpp"

namespace Slic3r::GUI::JusPrin {

namespace {

wxString nozzle_text(double nozzle) { return wxString::Format("%g mm", nozzle); }

HeaderMenuItem separator()
{
    HeaderMenuItem item;
    item.separator = true;
    item.title     = true; // never focusable; the rule is the whole row
    return item;
}

HeaderMenuItem other_printer(const Home::PrinterEntry& printer)
{
    HeaderMenuItem row;
    row.label = wxString::FromUTF8(printer.name);
    switch (printer.connection_state) {
    case Home::ConnectionState::Online:
        if (printer.state == Home::PrinterState::Printing) {
            row.decoration.status = StatusTone::Busy;
            row.decoration.status_word = _L("Printing");
            if (printer.progress_percent >= 0)
                row.decoration.status_word += wxString::FromUTF8(" \xC2\xB7 ") +
                                              wxString::Format("%d%%", printer.progress_percent);
        } else {
            row.decoration.status = StatusTone::Positive;
            row.decoration.status_word = _L("Idle");
        }
        break;
    case Home::ConnectionState::Offline:
        row.decoration.status = StatusTone::Neutral;
        row.decoration.status_word = _L("Offline");
        break;
    case Home::ConnectionState::Connected:
        // A saved print host can be reached without reporting a live job.
        row.decoration.status = StatusTone::Positive;
        row.decoration.status_word = _L("Connected");
        break;
    case Home::ConnectionState::None:
        row.decoration.status = StatusTone::Neutral;
        row.decoration.status_word = _L("Not connected");
        break;
    }
    return row;
}

} // namespace

PrinterMenu::PrinterMenu(wxWindow* owner, const ShellTheme& theme, bool dark, Plater& plater)
    : m_plater(plater), m_owner(owner), m_theme(theme), m_dark(dark) {}

void PrinterMenu::open(wxWindow* owner, const ShellTheme& theme, bool dark, Plater& plater, HeaderButton& anchor)
{
    auto self = std::make_shared<PrinterMenu>(owner, theme, dark, plater);
    self->m_menu = new HeaderMenu(owner, theme, dark, {});
    show_root(self);
    self->m_menu->open(anchor);
    // No dismiss listener: the row callbacks own the controller, so it lives
    // until the last queued callback has run.
}

void PrinterMenu::show_root(const Ptr& self)
{
    auto* menu = self->m_menu.get();
    if (menu == nullptr) return; // the popup went away; nothing to rebuild
    const auto printer    = SetupCommands::current_printer();
    const auto connection = SetupCommands::printer_connection();
    const auto plates     = SetupCommands::bed_types();

    std::vector<HeaderMenuItem> rows;

    // Row 1: the machine and what it is doing. A state JusPrin cannot observe
    // reads as "Not connected" with a plain consequence, never as a guess and
    // never in a warning colour -- the chip makes no claim about the machine.
    HeaderMenuItem machine;
    machine.label           = printer.nickname.empty() ? _L("No printer selected") : printer.nickname;
    machine.decoration.bold = true;
    machine.enabled         = connection.monitor_available;
    switch (connection.state) {
    case SetupCommands::ConnectionState::Idle:
        machine.decoration.status      = StatusTone::Positive;
        machine.decoration.status_word = _L("Idle");
        machine.decoration.trailing    = HeaderIcon::Monitor;
        break;
    case SetupCommands::ConnectionState::Printing:
        machine.decoration.status      = StatusTone::Busy;
        machine.decoration.status_word = _L("Printing");
        machine.decoration.trailing    = HeaderIcon::Monitor;
        break;
    case SetupCommands::ConnectionState::Offline:
        machine.decoration.status      = StatusTone::Neutral;
        machine.decoration.status_word = _L("Offline");
        machine.decoration.trailing    = HeaderIcon::Monitor;
        break;
    case SetupCommands::ConnectionState::NotConnected:
        machine.decoration.status      = StatusTone::Neutral;
        machine.decoration.status_word = _L("Not connected");
        // Connect… is deferred to a later slice, so the sub-line states the
        // consequence rather than offering an action that would do nothing.
        machine.decoration.sub_label = _L("Prints go to a card.");
        break;
    }
    if (connection.monitor_available)
        machine.invoke = [] { SetupCommands::open_monitor(); };
    rows.push_back(std::move(machine));
    rows.push_back(separator());

    // Read-only: a printer's nozzle is fixed by its parent system profile, and
    // changing it here would mean moving the printer to another parent and
    // deciding which of its own settings follow. Prepare only shows it.
    HeaderMenuItem nozzle;
    nozzle.label             = _L("Nozzle");
    nozzle.decoration.detail = nozzle_text(printer.nozzle);
    nozzle.enabled           = false;
    rows.push_back(std::move(nozzle));

    HeaderMenuItem plate;
    plate.label = _L("Plate");
    for (const auto& choice : plates)
        if (choice.current) plate.decoration.detail = choice.label;
    plate.enabled = !plates.empty();
    if (plate.enabled) {
        plate.decoration.trailing = HeaderIcon::Right;
        plate.keeps_open          = true;
        plate.invoke              = [self] { show_plates(self); };
    }
    rows.push_back(std::move(plate));
    rows.push_back(separator());

    // Home already resolves each saved printer's device or print-host state.
    // Use that same reading here so an unselected printer does not silently
    // become "Idle" just because its profile exists.
    const auto saved = installed_shell()->home_view()->backend().printers();
    bool has_other_printers = false;
    for (const Home::PrinterEntry& entry : saved) {
        if (entry.kind != Home::PrinterKind::Named || entry.name == printer.preset_name)
            continue;
        if (!has_other_printers) {
            HeaderMenuItem title;
            title.label = _L("OTHER PRINTERS");
            title.title = true;
            rows.push_back(std::move(title));
            has_other_printers = true;
        }
        HeaderMenuItem other = other_printer(entry);
        other.invoke = [self, name = entry.name] {
            if (!SetupCommands::select_named_printer(self->m_plater, name))
                return; // Orca kept the current printer, including when its prompt was cancelled.
            if (ShellController* shell = installed_shell())
                shell->status_row()->refresh();
        };
        rows.push_back(std::move(other));
    }
    if (has_other_printers)
        rows.push_back(separator());

    // Both of these open the printer conversation. Settings are about the
    // selected preset, named as Orca keys it: the header shows a stock
    // profile's model instead, which names no preset. Orca's placeholder is
    // no printer at all, so its settings are adding one.
    const bool placeholder = wxGetApp().preset_bundle->printers.get_selected_preset().is_default;
    HeaderMenuItem settings;
    settings.label  = _L("Printer settings…");
    settings.invoke = [preset = placeholder ? std::string() : printer.preset_name] {
        if (ShellController* shell = installed_shell())
            shell->open_printer_conversation(preset);
    };
    rows.push_back(std::move(settings));

    HeaderMenuItem add;
    add.label  = _L("Add a printer…");
    add.invoke = [] {
        if (ShellController* shell = installed_shell())
            shell->open_printer_conversation();
    };
    rows.push_back(std::move(add));

    menu->replace_items(std::move(rows));
}

void PrinterMenu::show_plates(const Ptr& self)
{
    std::vector<HeaderMenuItem> choices;
    for (const auto& choice : SetupCommands::bed_types()) {
        HeaderMenuItem row;
        row.label            = choice.label;
        row.decoration.check = choice.current;
        row.invoke = [self, value = choice.value] { SetupCommands::select_bed_type(self->m_plater, value); };
        choices.push_back(std::move(row));
    }
    show_sublist(self, _L("Plate"), std::move(choices));
}

void PrinterMenu::show_sublist(const Ptr& self, const wxString& title, std::vector<HeaderMenuItem> choices)
{
    auto* menu = self->m_menu.get();
    if (menu == nullptr) return; // the popup went away; nothing to rebuild
    std::vector<HeaderMenuItem> rows;
    HeaderMenuItem back;
    back.label      = title;
    back.icon       = HeaderIcon::Back;
    back.keeps_open = true;
    back.invoke     = [self] { show_root(self); };
    rows.push_back(std::move(back));
    rows.push_back(separator());
    for (auto& choice : choices) rows.push_back(std::move(choice));
    menu->replace_items(std::move(rows));
}

} // namespace Slic3r::GUI::JusPrin
