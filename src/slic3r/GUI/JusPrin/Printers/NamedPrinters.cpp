// First: slic3r/GUI/I18N.hpp defines _L only while the _ macro is still
// undefined, so it has to precede any header that brings one in.
#include "slic3r/GUI/I18N.hpp"

#include "NamedPrinters.hpp"
#include "PrinterNames.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Shell/SetupCommands.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SpoolStore.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Tab.hpp"

#include <boost/algorithm/string/predicate.hpp>

#include <map>
#include <set>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin::Printers {

namespace {

// device id -> printer name. Keyed by the id because AppConfig keys must be
// trimmed single-spaced strings and a printer name need not be.
constexpr const char* kDeviceSection = "jusprin_printer_devices";

PresetBundle& bundle()
{
    PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr)
        throw std::logic_error("Named printers used before the preset bundle exists");
    return *presets;
}

Tab& printer_tab()
{
    Tab* tab = wxGetApp().get_tab(Preset::TYPE_PRINTER);
    if (tab == nullptr)
        throw std::logic_error("Named printers used before the printer tab exists");
    return *tab;
}

std::map<std::string, std::string> device_links()
{
    AppConfig* config = wxGetApp().app_config;
    if (config == nullptr || !config->has_section(kDeviceSection))
        return {};
    return config->get_section(kDeviceSection);
}

void relink_device(const std::string& from, const std::string& to)
{
    AppConfig* config = wxGetApp().app_config;
    for (const auto& [device, name] : device_links()) {
        if (name != from)
            continue;
        if (to.empty())
            config->erase(kDeviceSection, device);
        else
            config->set(kDeviceSection, device, to);
    }
}

// The saved profile, never the edited copy the collection hands out for the
// selected one.
Preset* saved_preset(const std::string& name) { return bundle().printers.find_preset(name, false, true); }

bool is_selected(const std::string& name) { return bundle().printers.get_selected_preset_name() == name; }

// Profile files share a directory, so names that differ only in case are the
// same file on Windows and macOS.
bool name_in_use(const std::string& name)
{
    for (const Preset& preset : bundle().printers)
        if (boost::iequals(preset.name, name))
            return true;
    return false;
}

// SavePresetDialog::Item::update's reserved names.
bool name_reserved(const std::string& name)
{
    const PresetCollection& printers = bundle().printers;
    return name.find(PresetCollection::get_suffix_modified()) != std::string::npos || name == "Default Setting" ||
           name == PresetBundle::ORCA_DEFAULT_FILAMENT_PLACEHOLDER || name == "Default Printer" ||
           printers.get_preset_name_by_alias(name) != name;
}

wxString describe(NameProblem problem, const std::string& name)
{
    switch (problem) {
    case NameProblem::None: return {};
    case NameProblem::Empty: return _L("Enter a name for the printer.");
    case NameProblem::EdgeSpace: return _L("A printer name can't start or end with a space.");
    case NameProblem::IllegalCharacter:
        return wxString::Format(_L("A printer name can't contain any of these characters: %s"),
                                wxString::FromUTF8(kIllegalNameCharacters));
    case NameProblem::Reserved: return _L("That name is reserved. Choose another.");
    case NameProblem::Taken:
        return wxString::Format(_L("Another printer or profile is already named \"%s\"."), wxString::FromUTF8(name));
    }
    throw std::logic_error("Unhandled printer name problem");
}

wxString gone() { return _L("This printer no longer exists."); }

// Orca's order for an unselected profile, as CreatePresetsDialog deletes one:
// queue the cloud deletion, then drop the profile and its files.
void delete_unselected(const std::string& name)
{
    PresetBundle& presets = bundle();
    Preset*       preset  = saved_preset(name);
    if (preset == nullptr || is_selected(name))
        throw std::logic_error("delete_unselected called for a missing or selected printer");
    if (!preset->setting_id.empty())
        wxGetApp().delete_preset_from_cloud(preset->setting_id, preset->file);
    // PresetCollection::delete_preset erases from the list without moving the
    // selection index, so deleting a profile listed before the selected one
    // leaves the index on the selected one's neighbour. Select it again by
    // name, keeping its unsaved edits: selecting resets the edited copy.
    const std::string        selected = presets.printers.get_selected_preset_name();
    const DynamicPrintConfig edits    = presets.printers.get_edited_preset().config;
    if (!presets.printers.delete_preset(name))
        throw std::runtime_error("Orca refused to delete printer profile " + name);
    presets.printers.select_preset_by_name(selected, true);
    if (presets.printers.get_selected_preset_name() != selected)
        throw std::runtime_error("Printer profile " + selected + " could not be selected again");
    presets.printers.get_edited_preset().config = edits;
    printer_tab().update_tab_ui(true);
    if (MainFrame* frame = wxGetApp().mainframe)
        frame->update_side_preset_ui();
}

// SetupCommands::select_printer_preset returns true when the profile exists;
// the person can still cancel Orca's unsaved-changes prompt, so the selection
// is what says whether it happened.
bool select(Plater& plater, const std::string& name)
{
    SetupCommands::select_printer_preset(plater, name);
    return is_selected(name);
}

// Orca's own prompt before the selected printer profile is saved or replaced.
// True when nothing is unsaved, or once the person has saved or discarded it.
bool settle_unsaved_changes(const std::string& name)
{
    if (!bundle().printers.current_is_dirty())
        return true;
    return printer_tab().may_discard_current_dirty_preset(nullptr, name, /*no_transfer=*/true);
}

} // namespace

bool is_named_printer(const Preset& preset) { return preset.type == Preset::TYPE_PRINTER && preset.is_user() && preset.is_visible; }

std::vector<NamedPrinter> named_printers()
{
    std::map<std::string, std::string> device_of;
    for (const auto& [device, name] : device_links())
        device_of[name] = device;

    const PresetCollection& printers = bundle().printers;
    std::vector<NamedPrinter> result;
    for (const Preset& preset : printers) {
        if (!is_named_printer(preset))
            continue;
        NamedPrinter printer;
        printer.name     = preset.name;
        printer.selected = preset.name == printers.get_selected_preset_name();
        if (const auto found = device_of.find(preset.name); found != device_of.end())
            printer.device_id = found->second;
        if (const auto* nozzle = preset.config.option<ConfigOptionFloats>("nozzle_diameter"); nozzle && !nozzle->values.empty())
            printer.nozzle = nozzle->values.front();
        result.push_back(std::move(printer));
    }
    return result;
}

std::string add_named_printer(Plater& plater, const std::string& base_name, const std::string& device_id)
{
    if (!bundle().printers.get_selected_preset().is_system)
        throw std::logic_error("A named printer is added from the selected system profile");
    // A device that already has a printer is that printer: adding it again
    // selects it rather than creating a second card for one machine.
    if (!device_id.empty()) {
        const auto links = device_links();
        if (const auto found = links.find(device_id); found != links.end()) {
            const Preset* existing = saved_preset(found->second);
            if (existing != nullptr && is_named_printer(*existing) && select(plater, existing->name))
                return existing->name;
        }
    }
    const std::string name = first_free_printer_name(base_name, name_in_use);
    // A non-empty name skips SavePresetDialog; the profile inherits the
    // selected system profile, and Orca selects it and refreshes the plater.
    printer_tab().save_preset(name);
    if (!is_selected(name))
        throw std::runtime_error("Orca did not save the new printer profile " + name);
    if (!device_id.empty())
        wxGetApp().app_config->set(kDeviceSection, device_id, name);
    return name;
}

wxString rename_named_printer(Plater& plater, Workspace::SpoolStore* spools, const std::string& from,
                              const std::string& to)
{
    const Preset* preset = saved_preset(from);
    if (preset == nullptr || !is_named_printer(*preset))
        return gone();
    if (to == from)
        return {};
    // Saving "garage" over "Garage" writes the file being deleted next on a
    // case-insensitive file system.
    if (boost::iequals(to, from))
        return _L("A new name has to differ from the old one by more than capitalization.");
    const NameProblem problem = check_printer_name(to, name_reserved, name_in_use);
    if (problem != NameProblem::None)
        return describe(problem, to);

    // Orca has no rename: a profile is saved under the new name and the old
    // one deleted, which is what a person does by hand in the settings tab.
    // Saving works on the selected profile, so an unselected printer is
    // selected for the duration.
    const std::string previous = bundle().printers.get_selected_preset_name();
    if (previous == from) {
        if (!settle_unsaved_changes(from))
            return {};
    } else if (!select(plater, from)) {
        return {};
    }
    printer_tab().save_preset(to);
    if (!is_selected(to))
        throw std::runtime_error("Orca did not save printer profile " + from + " as " + to);
    delete_unselected(from);
    relink_device(from, to);
    if (spools != nullptr)
        spools->move_printer(from, to);
    if (previous != from)
        select(plater, previous);
    return {};
}

wxString remove_named_printer(Plater& plater, Workspace::SpoolStore* spools, const std::string& name)
{
    const Preset* preset = saved_preset(name);
    if (preset == nullptr || !is_named_printer(*preset))
        return gone();
    for (const Preset& other : bundle().printers)
        if (other.inherits() == name)
            return wxString::Format(_L("Other printer profiles are based on \"%s\". Remove those first in Printer settings."),
                                    wxString::FromUTF8(name));

    // A profile with no parent is a custom printer. Deleting one also deletes
    // the filament and process profiles made for it, and Orca's own delete
    // flow is what says so; the person may still decline there.
    const bool custom = preset->inherits().empty();
    if (custom) {
        const std::string previous = bundle().printers.get_selected_preset_name();
        if (previous != name && !select(plater, name))
            return {};
        printer_tab().delete_preset();
        if (saved_preset(name) != nullptr) {
            if (previous != name)
                select(plater, previous);
            return {};
        }
    } else if (is_selected(name)) {
        // Tab::select_preset's delete path, without Tab::delete_preset's
        // confirmation: the page asked already. It selects the profile this
        // one inherits, as Orca does.
        Tab& tab = printer_tab();
        plater.update_objects_position_when_select_preset([&] {
            tab.select_preset("", true);
            plater.on_config_change(bundle().full_config());
        });
    } else {
        delete_unselected(name);
    }

    if (saved_preset(name) != nullptr)
        throw std::runtime_error("Orca did not delete printer profile " + name);
    relink_device(name, {});
    if (spools != nullptr)
        spools->remove_printer(name);
    return {};
}

wxString open_named_printer_settings(Plater& plater, const std::string& name)
{
    const Preset* preset = saved_preset(name);
    if (preset == nullptr || !is_named_printer(*preset))
        return gone();
    if (select(plater, name))
        SetupCommands::open_settings_tab(Preset::TYPE_PRINTER);
    return {};
}

std::vector<std::string> name_installed_printers(Plater& plater, const VendorMap& before)
{
    std::vector<std::string> added;
    PresetBundle&     presets  = bundle();
    const std::string selected = presets.printers.get_selected_preset_name();
    const Preset&     current  = presets.printers.get_selected_preset();
    const auto        models   = newly_installed_models(before, wxGetApp().app_config->vendors(),
                                                        current.config.opt_string("printer_model"),
                                                        current.config.opt_string("printer_variant"));
    std::string keep;
    for (const InstalledModel& model : models) {
        const Preset* profile = presets.printers.find_system_preset_by_model_and_variant(model.model, model.variant);
        if (profile == nullptr)
            throw std::runtime_error("The wizard enabled " + model.model + " " + model.variant +
                                     " but no system profile for it is loaded");
        const std::string profile_name = profile->name;
        if (!SetupCommands::select_printer_preset(plater, profile_name) ||
            presets.printers.get_selected_preset_name() != profile_name)
            break; // keep the receipt for any printers already saved
        const std::string name = add_named_printer(plater, model.model, {});
        added.push_back(name);
        if (profile_name == selected)
            keep = name;
    }
    // Naming selects each printer in turn; the wizard's own choice is the one
    // left selected, under its name when it was one of them.
    if (!models.empty())
        SetupCommands::select_printer_preset(plater, keep.empty() ? selected : keep);
    return added;
}

wxString link_named_printer(const std::string& name, const std::string& device_id)
{
    const Preset* preset = saved_preset(name);
    if (preset == nullptr || !is_named_printer(*preset))
        return gone();
    if (device_id.empty())
        throw std::invalid_argument("A printer link needs a device id");
    for (const auto& [device, linked_name] : device_links()) {
        if (device == device_id && linked_name != name && saved_preset(linked_name) != nullptr)
            return _L("This device is already linked to another printer. Open that printer to connect it.");
        if (linked_name == name && device != device_id)
            return _L("This printer is already linked to another device.");
    }
    wxGetApp().app_config->set(kDeviceSection, device_id, name);
    return {};
}

wxString configure_named_printer_host(const std::string& name, PrintHostType type, const std::string& address,
                                     const std::string& api_key)
{
    auto& printers = bundle().printers;
    Preset* saved = saved_preset(name);
    if (!saved || !is_named_printer(*saved))
        return gone();
    if (is_selected(name) && printers.current_is_dirty())
        return _L("The open project has unsaved printer changes. Save or discard them in Printer settings, then connect again.");
    // Same persistence boundary as changing a named printer's nozzle. Patch
    // only connection options; never select another project's printer.
    saved->config.set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(type));
    saved->config.set_key_value("print_host", new ConfigOptionString(address));
    saved->config.set_key_value("printhost_apikey", new ConfigOptionString(api_key));
    const Preset* parent = printers.get_preset_parent(*saved);
    DynamicPrintConfig parent_config = parent ? parent->config : DynamicPrintConfig();
    saved->save(parent ? &parent_config : nullptr);
    saved->sync_info = "update";
    saved->save_info();
    if (is_selected(name)) {
        for (const char* key : {"host_type", "print_host", "printhost_apikey"})
            printers.get_edited_preset().config.set_key_value(key, saved->config.option(key)->clone());
        printer_tab().reload_config();
        printer_tab().update_tab_ui();
    }
    return {};
}

wxString change_named_printer_nozzle(Plater& plater, const std::string& name, const std::string& system_preset)
{
    PresetCollection& printers = bundle().printers;
    Preset*           saved    = saved_preset(name);
    if (saved == nullptr || !is_named_printer(*saved))
        return gone();

    const Preset* old_parent = printers.get_preset_parent(*saved);
    // Not const: Preset::save takes its parent's config by pointer.
    Preset*       new_parent = printers.find_preset(system_preset, false, true);
    if (old_parent == nullptr || new_parent == nullptr || !new_parent->is_system)
        return _L("This printer does not come with that nozzle size.");
    if (old_parent == new_parent)
        return {};

    // The open project keeps the printer it has selected, and nothing asks
    // the person anything: Orca's save and its unsaved-changes prompt both go
    // through the selection, so the profile is written here directly. When
    // the project uses this printer its copy follows the saved profile,
    // which would throw away edits nobody has saved yet; those are the
    // person's to settle first.
    const bool in_use = is_selected(name);
    if (in_use && printers.current_is_dirty())
        return _L("The open project has unsaved changes to this printer. Save or discard them in Printer settings, then try "
                  "again.");

    // The new nozzle's profile, plus every setting the person changed on this
    // printer -- except the ones the two nozzle profiles themselves disagree
    // on, which belong to the nozzle.
    DynamicPrintConfig          config  = new_parent->config;
    const auto                  changed = PresetCollection::dirty_options(saved, old_parent);
    const auto                  nozzle  = PresetCollection::dirty_options(new_parent, old_parent);
    const std::set<std::string> nozzle_keys(nozzle.begin(), nozzle.end());
    for (const std::string& key : changed)
        if (nozzle_keys.count(key) == 0)
            config.set_key_value(key, saved->config.option(key)->clone());
    Preset::inherits(config)                                         = new_parent->name;
    config.option<ConfigOptionString>("printer_settings_id", true)->value = name;
    Preset::normalize_inherits(config, new_parent);

    // What PresetCollection::save_current_preset and Tab::save_preset do for
    // the selected profile: store the difference from the parent, and queue
    // the update for the cloud.
    saved->config  = std::move(config);
    saved->base_id = new_parent->setting_id;
    saved->save(&new_parent->config);
    saved->sync_info = "update";
    saved->save_info();

    if (in_use) {
        // The same printer, under the same name: only its settings moved.
        printers.get_edited_preset().config = saved->config;
        plater.update_objects_position_when_select_preset([&] { plater.on_config_change(bundle().full_config()); });
        printer_tab().reload_config();
        printer_tab().update_tab_ui();
    }
    return {};
}

} // namespace Slic3r::GUI::JusPrin::Printers
