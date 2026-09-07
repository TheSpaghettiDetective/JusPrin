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

PrinterMenu::PrinterMenu(wxWindow* owner, const ShellTheme& theme, bool dark, Plater& plater)
    : m_owner(owner), m_theme(theme), m_dark(dark), m_plater(plater)
{}

void PrinterMenu::open(wxWindow* owner, const ShellTheme& theme, bool dark, Plater& plater, HeaderButton& anchor)
{
    auto* controller = new PrinterMenu(owner, theme, dark, plater);
    controller->m_menu = new HeaderMenu(owner, theme, dark, {});
    // The popup destroys itself on dismissal; the controller goes with it.
    controller->m_menu->set_dismiss_listener([controller] { delete controller; });
    controller->show_root();
    controller->m_menu->open(anchor);
}

void PrinterMenu::show_root()
{
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
        nozzle.invoke              = [this] { show_nozzles(); };
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
        plate.invoke              = [this] { show_plates(); };
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

    m_menu->replace_items(std::move(rows));
}

void PrinterMenu::show_nozzles()
{
    std::vector<HeaderMenuItem> choices;
    for (const auto& variant : SetupCommands::nozzle_variants()) {
        HeaderMenuItem row;
        row.label                = nozzle_text(variant.nozzle);
        row.decoration.check     = variant.current;
        row.invoke = [this, name = variant.preset_name] { SetupCommands::select_printer_preset(m_plater, name); };
        choices.push_back(std::move(row));
    }
    show_sublist(_L("Nozzle"), std::move(choices));
}

void PrinterMenu::show_plates()
{
    std::vector<HeaderMenuItem> choices;
    for (const auto& choice : SetupCommands::bed_types()) {
        HeaderMenuItem row;
        row.label            = choice.label;
        row.decoration.check = choice.current;
        row.invoke = [this, value = choice.value] { SetupCommands::select_bed_type(m_plater, value); };
        choices.push_back(std::move(row));
    }
    show_sublist(_L("Plate"), std::move(choices));
}

void PrinterMenu::show_sublist(const wxString& title, std::vector<HeaderMenuItem> choices)
{
    std::vector<HeaderMenuItem> rows;
    HeaderMenuItem back;
    back.label      = title;
    back.icon       = HeaderIcon::Back;
    back.keeps_open = true;
    back.invoke     = [this] { show_root(); };
    rows.push_back(std::move(back));
    rows.push_back(separator());
    for (auto& choice : choices) rows.push_back(std::move(choice));
    m_menu->replace_items(std::move(rows));
}

} // namespace Slic3r::GUI::JusPrin
