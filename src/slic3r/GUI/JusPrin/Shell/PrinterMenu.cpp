#include "PrinterMenu.hpp"
#include "SetupCommands.hpp"

#include "slic3r/GUI/ConfigWizard.hpp"
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

} // namespace

PrinterMenu::PrinterMenu(Plater& plater) : m_plater(plater) {}

void PrinterMenu::open(wxWindow* owner, const ShellTheme& theme, bool dark, Plater& plater, HeaderButton& anchor)
{
    auto self = std::make_shared<PrinterMenu>(plater);
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
    const auto variants   = SetupCommands::nozzle_variants();
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

    HeaderMenuItem nozzle;
    nozzle.label                = _L("Nozzle");
    nozzle.decoration.detail    = nozzle_text(printer.nozzle);
    nozzle.decoration.technical = true;
    nozzle.enabled              = !variants.empty();
    if (nozzle.enabled) {
        nozzle.decoration.trailing = HeaderIcon::Right;
        nozzle.keeps_open          = true;
        nozzle.invoke              = [self] { show_nozzles(self); };
    }
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

    HeaderMenuItem settings;
    settings.label  = _L("Printer settings…");
    settings.invoke = [] { SetupCommands::open_settings_tab(Preset::TYPE_PRINTER); };
    rows.push_back(std::move(settings));

    HeaderMenuItem add;
    add.label = _L("Add a printer…");
    // Deferred: this slice opens Orca's own wizard unchanged rather than the
    // three-step sheet the design calls for.
    add.invoke = [] { wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_PRINTERS); };
    rows.push_back(std::move(add));

    menu->replace_items(std::move(rows));
}

void PrinterMenu::show_nozzles(const Ptr& self)
{
    std::vector<HeaderMenuItem> choices;
    for (const auto& variant : SetupCommands::nozzle_variants()) {
        HeaderMenuItem row;
        row.label                = nozzle_text(variant.nozzle);
        row.decoration.check     = variant.current;
        row.invoke = [self, name = variant.preset_name] { SetupCommands::select_printer_preset(self->m_plater, name); };
        choices.push_back(std::move(row));
    }
    show_sublist(self, _L("Nozzle"), std::move(choices));
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
