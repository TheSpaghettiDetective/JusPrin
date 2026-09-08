#include "SetupCommands.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/Event.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/ParamsDialog.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Tab.hpp"

#include <algorithm>
#include <cmath>

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

} // namespace

PrinterInfo current_printer()
{
    PrinterInfo info;
    const PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr)
        return info;
    const Preset& printer = presets->printers.get_edited_preset();
    info.preset_name = printer.name;
    info.nickname    = wxString::FromUTF8(printer.config.opt_string("printer_model"));
    if (info.nickname.empty()) {
        info.nickname = wxString::FromUTF8(printer.label(false));
        // Custom profiles often encode the nozzle in their display name.
        const auto nozzle_suffix = info.nickname.Find(" nozzle");
        if (nozzle_suffix != wxNOT_FOUND) info.nickname = info.nickname.Left(nozzle_suffix).BeforeLast(' ');
    }
    if (const auto* nozzle = printer.config.option<ConfigOptionFloats>("nozzle_diameter"); nozzle && !nozzle->values.empty()) {
        info.nozzle         = nozzle->values.front();
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
    info.preset_name = presets->filament_presets.front();
    const Preset* preset = presets->filaments.find_preset(info.preset_name);
    if (preset == nullptr)
        return info;
    info.alias    = display_alias(*preset);
    info.vendor   = filament_vendor(*preset);
    info.material = option_string(preset->config, "filament_type");
    info.valid    = true;
    return info;
}

wxString current_colour()
{
    const PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr)
        return {};
    return option_string(presets->project_config, "filament_colour");
}

std::vector<NozzleVariant> nozzle_variants()
{
    std::vector<NozzleVariant> variants;
    const PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr)
        return variants;
    const Preset&     edited = presets->printers.get_edited_preset();
    const std::string model  = edited.config.opt_string("printer_model");
    if (model.empty())
        return variants;

    for (const Preset& preset : presets->printers) {
        if (!preset.is_visible || preset.is_default)
            continue;
        if (preset.config.opt_string("printer_model") != model)
            continue;
        const auto* nozzle = preset.config.option<ConfigOptionFloats>("nozzle_diameter");
        if (nozzle == nullptr || nozzle->values.empty())
            continue;
        variants.push_back({preset.name, nozzle->values.front(), preset.name == edited.name});
    }
    std::sort(variants.begin(), variants.end(),
              [](const NozzleVariant& a, const NozzleVariant& b) { return a.nozzle < b.nozzle; });
    // One variant is not a choice; the row shows the value without a submenu.
    if (variants.size() < 2)
        variants.clear();
    return variants;
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
        FilamentInfo info;
        info.preset_name = preset.name;
        info.alias       = display_alias(preset);
        info.vendor      = filament_vendor(preset);
        info.material    = option_string(preset.config, "filament_type");
        info.valid       = true;
        filaments.push_back(std::move(info));
    }
    return filaments;
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

bool select_filament_preset(Plater& plater, const std::string& preset_name)
{
    PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr || presets->filaments.find_preset(preset_name) == nullptr)
        return false;
    Tab* tab = wxGetApp().get_tab(Preset::TYPE_FILAMENT);
    if (tab == nullptr)
        return false;

    // The sequence Plater::priv::on_select_preset runs for a filament combo,
    // without the sidebar combo's own repaint and AMS badge. There is no
    // single public Orca call for "select filament preset N by name": the only
    // entry point is a wxEVT_COMBOBOX carrying a PlaterPresetComboBox as its
    // event object, and firing that would mean driving a hidden control.
    // If upstream grows such a call, this body should become one line.
    presets->set_filament_preset(0, preset_name);
    plater.update_project_dirty_from_presets();
    presets->export_selections(*wxGetApp().app_config);
    plater.on_filament_change(0);
    tab->select_preset(preset_name);
    plater.on_config_change(presets->full_config());
    return true;
}

void set_filament_colour(Plater& plater, const wxColour& colour)
{
    PresetBundle* presets = wxGetApp().preset_bundle;
    if (presets == nullptr || !colour.IsOk())
        return;
    DynamicPrintConfig& project = presets->project_config;

    // PlaterPresetComboBox::sync_colour_config, patching extruder 0. The three
    // options move together: a colour written without its type and
    // multi-colour siblings leaves a gradient's leftovers behind.
    auto patch = [&](const char* key, const std::string& value) -> ConfigOptionStrings* {
        auto* option = static_cast<ConfigOptionStrings*>(project.option(key)->clone());
        if (option->values.empty()) option->values.resize(1);
        option->values[0] = value;
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
    changed->SetInt(0);
    wxQueueEvent(&plater, changed);
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

void import_preset_file()
{
    // Orca's own Import Configs entry point: file dialog, overwrite
    // confirmation, and result message all belong to it.
    if (auto* frame = wxGetApp().mainframe; frame != nullptr)
        frame->load_config_file();
}

void open_monitor()
{
    if (auto* frame = wxGetApp().mainframe; frame != nullptr)
        frame->select_tab(size_t(MainFrame::tpMonitor));
}

const std::vector<Swatch>& swatches()
{
    // The twelve-swatch grid of the "new spool" step. Values are the semantic
    // filament colours the design calls for, not UI tokens: they stand for
    // physical filament, so they do not change between light and dark.
    static const std::vector<Swatch> palette{
        {"#1A1A1A", "Black"},  {"#F5F5F0", "White"},   {"#8C8C8C", "Grey"},  {"#C0392B", "Red"},
        {"#E67E22", "Orange"}, {"#F1C40F", "Yellow"},  {"#27AE60", "Green"}, {"#16A085", "Teal"},
        {"#2E6FD9", "Blue"},   {"#8E44AD", "Purple"},  {"#E084B7", "Pink"},  {"#7F8C8D", "Multicolour"}};
    return palette;
}

wxString colour_word(const wxColour& colour)
{
    if (!colour.IsOk())
        return {};
    // Nearest swatch in plain RGB distance. This only prefills a name the
    // person can edit, so a simple metric is the honest amount of machinery.
    const Swatch* best = nullptr;
    double best_distance = 0.;
    for (const Swatch& swatch : swatches()) {
        const wxColour candidate(wxString::FromUTF8(swatch.hex));
        const double dr = colour.Red() - candidate.Red();
        const double dg = colour.Green() - candidate.Green();
        const double db = colour.Blue() - candidate.Blue();
        const double distance = dr * dr + dg * dg + db * db;
        if (best == nullptr || distance < best_distance) {
            best          = &swatch;
            best_distance = distance;
        }
    }
    // Roughly a quarter of the diagonal of the RGB cube: past that, no word
    // describes the colour better than none.
    constexpr double kTooFar = 110. * 110. * 3.;
    if (best == nullptr || best_distance > kTooFar)
        return {};
    return _(best->name);
}

} // namespace Slic3r::GUI::JusPrin::SetupCommands
