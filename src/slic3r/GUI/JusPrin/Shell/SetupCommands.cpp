#include "SetupCommands.hpp"
#include "slic3r/GUI/JusPrin/Printers/NamedPrinters.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterDiscovery.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/Event.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/ParamsDialog.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/PresetComboBoxes.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/Utils/ColorSpaceConvert.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace Slic3r::GUI::JusPrin::SetupCommands {

namespace {

// The alias Orca shows in the sidebar: the preset's alias, with the
// "@<printer>" qualifier trimmed the way StatusRow already trims it.
wxString display_alias(const Preset& preset)
{
    wxString alias = wxString::FromUTF8(preset.alias.empty() ? preset.name : preset.alias);
    alias = alias.BeforeFirst('@');
    alias.Trim();
    return alias;
}

wxString option_string(const DynamicPrintConfig& config, const char* key, unsigned index = 0)
{
    if (const auto* option = config.option<ConfigOptionStrings>(key); option && option->values.size() > index)
        return wxString::FromUTF8(option->values[index]);
    return {};
}

// The brand printed on the reel. Orca resolves filament_vendor through the
// inheritance chain, so a branded profile reports its own name and everything
// that only inherits fdm_filament_common reports "Generic". Preset::vendor
// cannot answer this: it names the profile bundle the preset shipped in.
wxString filament_vendor(const Preset& preset)
{
    const wxString vendor = option_string(preset.config, "filament_vendor");
    // PrintConfig's stand-in for a preset that declares no vendor at all. It is
    // a placeholder, not a brand, so it reads here as no vendor.
    return vendor == "(Undefined)" ? wxString{} : vendor;
}

FilamentInfo filament_info(const Preset& preset)
{
    FilamentInfo info;
    info.preset_name = preset.name;
    info.alias       = display_alias(preset);
    info.vendor      = filament_vendor(preset);
    info.material    = option_string(preset.config, "filament_type");
    info.valid       = true;
    return info;
}

// The nozzle each slot feeds. Orca's filament_map names an extruder per slot
// on the printers that use it; a printer without one feeds every slot through
// its first nozzle.
std::vector<double> slot_nozzles(const PresetBundle& presets, const std::vector<double>& nozzles, std::size_t slots)
{
    std::vector<double> result(slots, nozzles.empty() ? 0. : nozzles.front());
    const auto* map = presets.project_config.option<ConfigOptionInts>("filament_map");
    if (nozzles.size() < 2 || map == nullptr)
        return result;
    for (std::size_t slot = 0; slot < slots && slot < map->values.size(); ++slot) {
        const int extruder = map->values[slot] - 1; // 1-based in the config
        if (extruder >= 0 && extruder < int(nozzles.size()))
            result[slot] = nozzles[extruder];
    }
    return result;
}

} // namespace

PrinterInfo current_printer()
{
    PrinterInfo info;
    const PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr)
        return info;
    const Preset& printer = presets->printers.get_edited_preset();
    info.preset_name = printer.name;
    // A named printer is called what the person called it; a system profile
    // is only a model.
    info.nickname = wxString::FromUTF8(Printers::is_named_printer(printer) ? printer.name
                                                                            : printer.config.opt_string("printer_model"));
    if (info.nickname.empty()) {
        info.nickname = wxString::FromUTF8(printer.label(false));
        // Custom profiles often encode the nozzle in their display name.
        const auto nozzle_suffix = info.nickname.Find(" nozzle");
        if (nozzle_suffix != wxNOT_FOUND) info.nickname = info.nickname.Left(nozzle_suffix).BeforeLast(' ');
    }
    if (const auto* nozzle = printer.config.option<ConfigOptionFloats>("nozzle_diameter"); nozzle && !nozzle->values.empty()) {
        info.nozzle         = nozzle->values.front();
        info.nozzles        = nozzle->values;
        info.extruder_count = nozzle->values.size();
    }
    info.valid = true;
    return info;
}

FilamentInfo current_filament()
{
    FilamentInfo info;
    const PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr || presets->filament_presets.empty())
        return info;
    if (const Preset* preset = presets->filaments.find_preset(presets->filament_presets.front()))
        return filament_info(*preset);
    info.preset_name = presets->filament_presets.front();
    return info;
}

wxString current_colour()
{
    const PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr)
        return {};
    return option_string(presets->project_config, "filament_colour");
}

std::vector<FilamentSlot> filament_slots()
{
    std::vector<FilamentSlot> slots;
    const PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr)
        return slots;
    // The plate's own answer, from the model and the config, so it holds
    // before anything is sliced. 1-based, like the slots on screen.
    std::set<int> used;
    if (Plater* plater = wxGetApp().plater())
        if (PartPlate* plate = plater->get_partplate_list().get_curr_plate())
            for (int extruder : plate->get_extruders())
                used.insert(extruder);

    const PrinterInfo printer = current_printer();
    const bool        mixed   = std::adjacent_find(printer.nozzles.begin(), printer.nozzles.end(),
                                                   std::not_equal_to<double>()) != printer.nozzles.end();
    const std::vector<double> nozzles = slot_nozzles(*presets, printer.nozzles, presets->filament_presets.size());
    for (std::size_t index = 0; index < presets->filament_presets.size(); ++index) {
        FilamentSlot slot;
        slot.index = index;
        if (const Preset* preset = presets->filaments.find_preset(presets->filament_presets[index]))
            slot.filament = filament_info(*preset);
        else
            slot.filament.preset_name = presets->filament_presets[index];
        slot.colour = option_string(presets->project_config, "filament_colour", unsigned(index));
        slot.used   = used.count(int(index) + 1) > 0;
        slot.nozzle = mixed ? nozzles[index] : 0.;
        slots.push_back(std::move(slot));
    }
    return slots;
}

std::vector<BedTypeChoice> bed_types()
{
    std::vector<BedTypeChoice> choices;
    auto* plater = wxGetApp().plater();
    if (plater == nullptr)
        return choices;

    // Orca filters the list per printer model; read its filtered list rather
    // than enumerating the BedType enum, or the menu would offer plates this
    // machine does not have.
    const std::vector<BedType>& supported = plater->sidebar().get_cur_combox_bed_types();
    if (supported.size() < 2)
        return choices;
    const BedType current = plater->sidebar().get_cur_select_bed_type();

    const ConfigOptionDef* definition = print_config_def.get("curr_bed_type");
    for (BedType type : supported) {
        wxString label;
        if (definition != nullptr) {
            // enum_values is 0-based over the labels that follow btDefault.
            const int index = int(type) - 1;
            if (index >= 0 && index < int(definition->enum_labels.size()))
                label = _(definition->enum_labels[index]);
        }
        if (label.empty())
            label = wxString::Format("%d", int(type));
        choices.push_back({int(type), label, type == current});
    }
    return choices;
}

std::vector<FilamentInfo> compatible_filaments()
{
    std::vector<FilamentInfo> filaments;
    const PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr)
        return filaments;
    for (const Preset& preset : presets->filaments) {
        // is_compatible is maintained by PresetBundle::update_compatible; the
        // compatibility rule is never re-derived here.
        if (!preset.is_visible || !preset.is_compatible || preset.is_default)
            continue;
        filaments.push_back(filament_info(preset));
    }
    // Brand, then name, as the design lists them. The short name usually
    // starts with the brand already, so ties in brand fall through to it.
    std::stable_sort(filaments.begin(), filaments.end(), [](const FilamentInfo& a, const FilamentInfo& b) {
        const int brand = a.vendor.CmpNoCase(b.vendor);
        return brand != 0 ? brand < 0 : a.alias.CmpNoCase(b.alias) < 0;
    });
    return filaments;
}

std::vector<wxColour> saved_colours()
{
    std::vector<wxColour> colours;
    if (AppConfig* config = wxGetApp().app_config)
        for (const std::string& value : config->get_custom_color_from_config()) {
            const wxColour colour = string_to_wxColor(value);
            // The system picker keeps a fixed number of slots and fills the
            // unused ones with one colour; a repeat says nothing new.
            if (colour.IsOk() && std::find(colours.begin(), colours.end(), colour) == colours.end())
                colours.push_back(colour);
        }
    return colours;
}

void remember_colour(const wxColour& colour)
{
    AppConfig* config = wxGetApp().app_config;
    if (config == nullptr || !colour.IsOk())
        return;
    // The same list, in the same form, that Orca's own colour fields read and
    // write (ColourPicker::save_colors_to_config), so both pickers share it.
    // The system picker has sixteen custom slots; the list never shrinks,
    // because saving overwrites entries by position and leaves any beyond.
    std::vector<std::string> values   = config->get_custom_color_from_config();
    const std::size_t        original = values.size();
    const std::size_t        slots    = std::max<std::size_t>(original, 16);
    values.erase(std::remove_if(values.begin(), values.end(),
                                [&](const std::string& value) { return string_to_wxColor(value) == colour; }),
                 values.end());
    values.insert(values.begin(), color_to_string(colour));
    if (values.size() > slots)
        values.resize(slots);
    // Refill what removing repeats freed, so no stale entry survives past the
    // end; a repeat reads back as nothing new.
    while (values.size() < original)
        values.push_back(values.back());
    config->save_custom_color_to_config(values);
}

PrinterConnection printer_connection()
{
    PrinterConnection connection;
    // Non-const: use_bbl_network() is not a const member on PresetBundle.
    PresetBundle* presets = wxGetApp().preset_bundle;
    // Only the Bambu network path reports a live machine state. Every other
    // print host is "not connected" here, which is a statement about what
    // JusPrin can observe, not a claim about the machine.
    if (presets == nullptr || !presets->use_bbl_network())
        return connection;
    auto* devices = wxGetApp().getDeviceManager();
    if (devices == nullptr)
        return connection;
    MachineObject* machine = devices->get_selected_machine();
    if (machine == nullptr)
        return connection;
    connection.monitor_available = true;
    if (!machine->is_connected())
        connection.state = ConnectionState::Offline;
    else if (machine->is_in_printing())
        connection.state = ConnectionState::Printing;
    else
        connection.state = ConnectionState::Idle;
    return connection;
}

TrayState printer_trays()
{
    const PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr)
        return TrayState::None;
    // Trays belong to the device a named printer was connected to; a printer
    // with no device reports nothing.
    const std::string& selected = presets->printers.get_edited_preset().name;
    std::string        device_id;
    for (const Printers::NamedPrinter& named : Printers::named_printers())
        if (named.name == selected)
            device_id = named.device_id;
    if (device_id.empty())
        return TrayState::None;
    for (const PrinterSetup::DiscoveredPrinter& device : PrinterSetup::discover_printers(true))
        if (device.stable_id == device_id) {
            if (device.connected)
                return device.spools.empty() ? TrayState::None : TrayState::Available;
            return TrayState::Offline;
        }
    return PrinterSetup::has_verified_printer_connection(device_id) ? TrayState::Offline : TrayState::None;
}

bool select_filament_preset(Plater& plater, std::size_t slot, const std::string& preset_name)
{
    PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr || slot >= presets->filament_presets.size())
        return false;
    const Preset* preset = presets->filaments.find_preset(preset_name);
    Tab*          tab    = wxGetApp().get_tab(Preset::TYPE_FILAMENT);
    if (preset == nullptr || tab == nullptr)
        return false;

    // The sequence the sidebar's filament combo runs, without its own repaint
    // and AMS badge. There is no single public Orca call for "select filament
    // preset N by name": the only entry point is a wxEVT_COMBOBOX carrying a
    // PlaterPresetComboBox as its event object, and firing that would mean
    // driving a hidden control. If upstream grows such a call, this body
    // should become one line.
    //
    // First PresetComboBox::update_ams_color, which OnSelect runs before the
    // selection reaches Plater: a preset that names a default colour brings it
    // to the slot; one that names none leaves the slot's colour as it was.
    const wxString default_colour = option_string(preset->config, "default_filament_colour");
    if (!default_colour.empty())
        set_filament_colour(plater, slot, wxColour(default_colour));

    // Then the filament branch of Plater::priv::on_select_preset.
    const bool was_support = is_support_filament(int(slot));
    presets->set_filament_preset(slot, preset_name);
    plater.update_project_dirty_from_presets();
    presets->export_selections(*wxGetApp().app_config);
    plater.sidebar().update_dynamic_filament_list();
    if (was_support != is_support_filament(int(slot)) && wxGetApp().app_config->get("auto_calculate_flush") == "all")
        plater.sidebar().auto_calc_flushing_volumes(int(slot));
    plater.on_filament_change(slot);
    // With one slot the filament tab follows the selection; with several it
    // keeps its own, and only the slot lists repaint.
    if (presets->filament_presets.size() > 1)
        plater.sidebar().update_presets(Preset::TYPE_FILAMENT);
    else
        tab->select_preset(preset_name);
    plater.on_config_change(presets->full_config());
    return true;
}

void set_filament_colour(Plater& plater, std::size_t slot, const wxColour& colour)
{
    PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr || !colour.IsOk() || slot >= presets->filament_presets.size())
        return;
    DynamicPrintConfig& project = presets->project_config;

    // PlaterPresetComboBox::sync_colour_config for one slot. The three options
    // move together: a colour written without its type and multi-colour
    // siblings leaves a gradient's leftovers behind.
    auto patch = [&](const char* key, const std::string& value) -> ConfigOptionStrings* {
        auto* option = static_cast<ConfigOptionStrings*>(project.option(key)->clone());
        if (option->values.size() <= slot) option->values.resize(slot + 1);
        option->values[slot] = value;
        return option;
    };
    const std::string hex = colour.GetAsString(wxC2S_HTML_SYNTAX).ToStdString();

    DynamicPrintConfig updated = project;
    updated.set_key_value("filament_multi_colour", patch("filament_multi_colour", hex));
    updated.set_key_value("filament_colour", patch("filament_colour", hex));
    updated.set_key_value("filament_colour_type", patch("filament_colour_type", "1")); // solid, not gradient
    project.apply(updated);

    plater.update_project_dirty_from_presets();
    presets->export_selections(*wxGetApp().app_config);
    plater.on_config_change(updated);

    auto* changed = new wxCommandEvent(EVT_FILAMENT_COLOR_CHANGED);
    changed->SetInt(int(slot));
    wxQueueEvent(&plater, changed);
}

std::optional<std::size_t> add_filament_slot(Plater& plater)
{
    PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr)
        return std::nullopt;
    const std::size_t before = presets->filament_presets.size();
    // The sidebar's own "+": the next colour, the plate and object-list
    // updates, the flush volumes. It refuses at the slot limit and in a
    // G-code-only project, which the count reports.
    plater.sidebar().add_filament();
    plater.sidebar().update_filaments_counter();
    if (presets->filament_presets.size() <= before)
        return std::nullopt;
    return presets->filament_presets.size() - 1;
}

bool select_printer_preset(Plater& plater, const std::string& preset_name)
{
    PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr || presets->printers.find_preset(preset_name) == nullptr)
        return false;
    Tab* tab = wxGetApp().get_tab(Preset::TYPE_PRINTER);
    if (tab == nullptr)
        return false;
    if (presets->printers.get_edited_preset().name == preset_name)
        return true;

    // The printer branch of Plater::priv::on_select_preset. The wrapper is
    // Orca's, and it is what keeps objects on their plates when the bed shape
    // changes with the preset.
    plater.update_objects_position_when_select_preset([tab, preset_name, presets, &plater] {
        tab->select_preset(preset_name);
        plater.on_config_change(presets->full_config());
    });
    return true;
}

bool install_and_select_printer(Plater& plater, const std::string& vendor_id,
                                const std::string& model_id, const std::string& variant,
                                const std::string& default_filament, std::string& error)
{
    PresetBundle* presets = wxGetApp().preset_bundle;
    AppConfig* config = wxGetApp().app_config;
    if (!presets || !config || vendor_id.empty() || model_id.empty() || variant.empty()) {
        error = "Printer setup is unavailable because the preset system is not ready.";
        return false;
    }
    const auto old_vendors = config->vendors();
    const bool had_filaments = config->has_section(AppConfig::SECTION_FILAMENTS);
    const auto old_filaments = had_filaments ? config->get_section(AppConfig::SECTION_FILAMENTS)
                                             : std::map<std::string, std::string>();
    const std::string old_printer = presets->printers.get_selected_preset_name();
    auto rollback = [&]() -> std::string {
        config->set_vendors(old_vendors);
        config->set_section(AppConfig::SECTION_FILAMENTS, old_filaments);
        try {
            presets->load_presets(*config, ForwardCompatibilitySubstitutionRule::Enable,
                                  {std::string(), std::string(), std::string(), std::string()});
            if (!select_printer_preset(plater, old_printer))
                return "The previous printer preset could not be selected again.";
            if (wxGetApp().mainframe) wxGetApp().mainframe->update_side_preset_ui();
        } catch (const std::exception& exception) {
            return std::string("The previous preset configuration could not be reloaded: ") + exception.what();
        }
        return {};
    };
    try {
        std::map<std::string, std::map<std::string, std::set<std::string>>> vendors;
        vendors[vendor_id][model_id].insert(variant);
        std::map<std::string, std::string> filaments;
        if (!default_filament.empty()) filaments[default_filament] = "true";
        bool applied = false;
        bool selected = false;
        plater.update_objects_position_when_select_preset([&] {
            applied = presets->apply_vendor_config(vendors, filaments, config, false, model_id, variant,
                                                   default_filament);
            if (!applied) return;
            const Preset* preset = presets->printers.find_system_preset_by_model_and_variant(model_id, variant);
            Tab* tab = wxGetApp().get_tab(Preset::TYPE_PRINTER);
            if (!preset || !tab) return;
            tab->select_preset(preset->name);
            plater.on_config_change(presets->full_config());
            selected = true;
        });
        if (!applied) {
            const std::string rollback_error = rollback();
            error = rollback_error.empty()
                ? "The printer profile could not be installed. Your previous setup is unchanged."
                : "The printer profile could not be installed. " + rollback_error;
            return false;
        }
        if (!selected) {
            const std::string rollback_error = rollback();
            error = rollback_error.empty()
                ? "The installed profile could not be selected. Your previous setup is unchanged."
                : "The installed profile could not be selected. " + rollback_error;
            return false;
        }
        if (wxGetApp().mainframe) wxGetApp().mainframe->update_side_preset_ui();
        presets->export_selections(*config);
        return true;
    } catch (const std::exception& exception) {
        const std::string rollback_error = rollback();
        error = std::string("The printer profile could not be loaded: ") + exception.what();
        if (!rollback_error.empty()) error += " " + rollback_error;
        return false;
    }
}

bool select_bed_type(Plater& plater, int bed_type_value)
{
    const std::vector<BedType>& supported = plater.sidebar().get_cur_combox_bed_types();
    const auto found = std::find(supported.begin(), supported.end(), BedType(bed_type_value));
    if (found == supported.end())
        return false;
    // set_bed_type_accord_combox selects *and notifies*, so Orca's own
    // on_select_bed_type runs: it owns the project-config write, the
    // app-config record, the invalidation of every plate still on the default
    // bed, and the re-render. None of that is repeated here.
    //
    // Ownership note: a bed type is machine state to the person and project
    // state to Orca. The chip presents it under the printer because that is
    // where the person looks for it; the value still lives in the project.
    plater.sidebar().set_bed_type_accord_combox(BedType(bed_type_value));
    return true;
}

void open_settings_tab(Preset::Type type)
{
    Tab* tab = wxGetApp().get_tab(type);
    if (tab == nullptr)
        return;
    // PlaterPresetComboBox::switch_to_tab: the Tab activates its own page and
    // ParamsPanel, including clearing the previous page.
    wxGetApp().params_dialog()->Popup();
    tab->OnActivate();
    tab->restore_last_select_item();
}

void open_filament_settings(std::size_t slot)
{
    const PresetBundle* presets = wxGetApp().preset_bundle;
    Tab*                tab     = wxGetApp().get_tab(Preset::TYPE_FILAMENT);
    if (presets == nullptr || tab == nullptr || slot >= presets->filament_presets.size())
        return;
    // The filament half of PlaterPresetComboBox::switch_to_tab: the tab loads
    // this slot's preset and remembers which slot it is editing, so a save
    // lands on that slot.
    if (!tab->select_preset(presets->filament_presets[slot]))
        return;
    if (auto* combo = tab->get_combo_box())
        combo->set_filament_idx(int(slot));
    open_settings_tab(Preset::TYPE_FILAMENT);
}

void open_filament_library()
{
    // The page behind the sidebar combo's "Add/Remove filaments" entry
    // (PlaterPresetComboBox::OnSelect). run_wizard reloads the presets when
    // the page applies, so the installed list is current on return.
    wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_FILAMENTS);
}

bool sync_from_printer(Plater& plater)
{
    const PresetBundle* presets = wxGetApp().preset_bundle;
    DeviceManager*      devices = wxGetApp().getDeviceManager();
    if (presets == nullptr || devices == nullptr)
        return false;
    // Orca's sync reads the machine the device manager has selected. The
    // project is set up for a named printer, so that printer's device is the
    // one to read; selecting it is what the device list does on a click.
    const std::string& selected = presets->printers.get_edited_preset().name;
    for (const Printers::NamedPrinter& named : Printers::named_printers())
        if (named.name == selected && !named.device_id.empty()) {
            MachineObject* machine = devices->get_my_machine(named.device_id);
            if (machine == nullptr || !machine->is_connected())
                return false;
            if (devices->get_selected_machine() != machine)
                devices->set_selected_machine(named.device_id);
            // The filament list's own sync button, dialog and all.
            plater.sidebar().sync_ams_list();
            return true;
        }
    return false;
}

void open_monitor()
{
    if (auto* frame = wxGetApp().mainframe; frame != nullptr)
        frame->select_tab(size_t(MainFrame::tpMonitor));
}

} // namespace Slic3r::GUI::JusPrin::SetupCommands
