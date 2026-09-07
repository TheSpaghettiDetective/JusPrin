#pragma once

// The one place the printer/spool chip reads and changes Orca's machine and
// material setup.
//
// Every function here either reads the preset bundle or calls an existing
// public Orca entry point. Nothing writes a preset or a colour into the config
// on its own, because doing that skips the dirty tracking, the undo snapshot,
// the compatibility pass, and the background-process invalidation that the
// sidebar's own path performs.
//
// Collected in one file rather than spread across the chip and its two menus
// so the comments naming each upstream reference sequence exist once, and a
// future rebase has one place to check when those sequences move.

#include "libslic3r/Preset.hpp"

#include <wx/colour.h>
#include <wx/string.h>

#include <string>
#include <vector>

namespace Slic3r::GUI { class Plater; }

namespace Slic3r::GUI::JusPrin::SetupCommands {

struct PrinterInfo
{
    std::string preset_name;
    wxString    nickname;        // printer_model, else the label without " nozzle"
    double      nozzle{0.};
    std::size_t extruder_count{1};
    bool        valid{false};
};

struct FilamentInfo
{
    std::string preset_name;
    wxString    alias;           // display name, "@printer" suffix removed
    wxString    vendor;
    wxString    material;        // filament_type[0], e.g. "PLA"
    bool        valid{false};
};

// -- Reads ------------------------------------------------------------------

PrinterInfo  current_printer();
FilamentInfo current_filament();          // extruder 0
// The project's extruder-0 filament colour as "#RRGGBB", or empty.
wxString     current_colour();

struct NozzleVariant
{
    std::string preset_name;
    double      nozzle{0.};
    bool        current{false};
};
// Sibling printer presets for the same printer model that differ only in
// nozzle, smallest first.
std::vector<NozzleVariant> nozzle_variants();

struct BedTypeChoice
{
    int      value{0};           // BedType enum value
    wxString label;
    bool     current{false};
};
// The bed types this printer offers, as Orca's own sidebar list reports them.
// Empty when the printer does not support a bed-type choice.
std::vector<BedTypeChoice> bed_types();

// Filament presets that are visible and compatible with the current printer.
// Uses the same is_compatible flag Orca's sidebar filters on; the compatibility
// rule itself is never re-derived here.
std::vector<FilamentInfo> compatible_filaments();

enum class ConnectionState
{
    NotConnected, // no network printer, or a host Orca does not report state for
    Idle,
    Printing,
    Offline
};

struct PrinterConnection
{
    ConnectionState state{ConnectionState::NotConnected};
    // Whether the monitor tab has a machine to show.
    bool            monitor_available{false};
};
PrinterConnection printer_connection();

// -- Writes -----------------------------------------------------------------

// Switches the extruder-0 filament preset. Mirrors the filament branch of the
// private Plater::priv::on_select_preset, minus the sidebar combo's own
// presentation. Returns false when the preset does not exist.
bool select_filament_preset(Plater& plater, const std::string& preset_name);

// Sets the extruder-0 filament colour. Mirrors PlaterPresetComboBox::
// sync_colour_config, minus the combo's own repaint.
void set_filament_colour(Plater& plater, const wxColour& colour);

// Switches the printer preset. Mirrors the printer branch of
// Plater::priv::on_select_preset through Plater's public
// update_objects_position_when_select_preset.
bool select_printer_preset(Plater& plater, const std::string& preset_name);

// Applies a bed type through Sidebar::set_bed_type_accord_combox, the public
// method Orca itself uses; it notifies the combo, so Orca's own handler owns
// the app-config write, the slice invalidation, and the re-render.
bool select_bed_type(Plater& plater, int bed_type_value);

// Opens an Orca settings tab, exactly as PlaterPresetComboBox::switch_to_tab.
void open_settings_tab(Preset::Type type);
// Orca's own "Import Configs" flow, dialogs included.
void import_preset_file();
// The monitor tab, when a machine is connected.
void open_monitor();

// -- Naming -----------------------------------------------------------------

// The plain-language colour word for a hex value ("Cold White", "Red"), used
// to prefill a new spool's name. Empty when no word is close enough.
wxString colour_word(const wxColour& colour);

// The twelve-swatch palette the "new spool" step offers, in Figma's order.
struct Swatch { const char* hex; const char* name; };
const std::vector<Swatch>& swatches();

} // namespace Slic3r::GUI::JusPrin::SetupCommands
