#pragma once

// The one place the printer/filament chip reads and changes Orca's machine and
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

#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI { class Plater; }

namespace Slic3r::GUI::JusPrin::SetupCommands {

struct PrinterInfo
{
    std::string preset_name;
    wxString    nickname;        // a named printer's name, else printer_model, else the label without " nozzle"
    double      nozzle{0.};      // the first nozzle
    // Every nozzle, in extruder order. One entry on a single-nozzle printer.
    std::vector<double> nozzles;
    std::size_t extruder_count{1};
    bool        valid{false};
};

struct FilamentInfo
{
    std::string preset_name;
    wxString    alias;           // the short name: display name, "@printer" suffix removed
    // The filament's brand -- "Bambu Lab", "eSUN", "Generic" -- read from the
    // preset's own filament_vendor option. Not Preset::vendor, which names the
    // profile bundle the preset shipped in ("BBL", "OrcaFilamentLibrary") and
    // so can never identify an unbranded preset. Empty when the preset
    // declares no vendor.
    wxString    vendor;
    wxString    material;        // filament_type[0], e.g. "PLA"
    bool        valid{false};
};

// One of the project's numbered filament entries: one filament, one colour.
struct FilamentSlot
{
    std::size_t  index{0};       // 0-based; the person reads index + 1
    FilamentInfo filament;
    wxString     colour;         // "#RRGGBB", or empty
    // Whether the current plate prints with this slot, as Orca's own plate
    // reports it from the model -- before slicing, too.
    bool         used{false};
    // The nozzle this slot feeds, on a printer with nozzles of more than one
    // size; zero otherwise.
    double       nozzle{0.};
};

// -- Reads ------------------------------------------------------------------

PrinterInfo  current_printer();
FilamentInfo current_filament();          // slot 1
// Slot 1's colour as "#RRGGBB", or empty.
wxString     current_colour();

// Every filament slot in the project, in slot order.
std::vector<FilamentSlot> filament_slots();

struct BedTypeChoice
{
    int      value{0};           // BedType enum value
    wxString label;
    bool     current{false};
};
// The bed types this printer offers, as Orca's own sidebar list reports them.
// Empty when the printer does not support a bed-type choice.
std::vector<BedTypeChoice> bed_types();

// The installed filaments -- the short list the person switched on -- that
// fit the current printer and nozzle, brand then name. Uses the same
// is_visible and is_compatible flags Orca's sidebar filters on; neither rule
// is re-derived here.
std::vector<FilamentInfo> compatible_filaments();

// Colours the person saved in the colour picker, as Orca's own colour fields
// keep them, without repeats.
std::vector<wxColour> saved_colours();
// Adds a colour to the front of that list, as picking one in Orca does.
void remember_colour(const wxColour& colour);

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

// Whether the current printer reports what is loaded in it.
enum class TrayState
{
    None,      // it reports nothing: no row
    Available, // connected, and it says what it holds
    Offline    // it reports trays, but has not been heard from
};
TrayState printer_trays();

// -- Writes -----------------------------------------------------------------

// Switches one slot's filament preset. Follows the filament branch of the
// private Plater::priv::on_select_preset and the colour step before it in
// PresetComboBox::update_ams_color: a preset with a default colour brings it
// to the slot, one without leaves the slot's colour alone. Returns false when
// the preset or the slot does not exist.
bool select_filament_preset(Plater& plater, std::size_t slot, const std::string& preset_name);

// Sets one slot's colour. Mirrors PlaterPresetComboBox::sync_colour_config,
// minus the combo's own repaint.
void set_filament_colour(Plater& plater, std::size_t slot, const wxColour& colour);

// Adds a slot exactly as the sidebar's "+" does, and returns its index, or
// nothing when Orca refused (the slot limit, a G-code-only project).
std::optional<std::size_t> add_filament_slot(Plater& plater);

// Switches the printer preset. Mirrors the printer branch of
// Plater::priv::on_select_preset through Plater's public
// update_objects_position_when_select_preset.
bool select_printer_preset(Plater& plater, const std::string& preset_name);

// Enables one shipped vendor/model/variant and selects its real system preset.
// On a load failure the old AppConfig selections are restored and reloaded.
bool install_and_select_printer(Plater& plater, const std::string& vendor_id,
                                const std::string& model_id, const std::string& variant,
                                const std::string& default_filament, std::string& error);

// Applies a bed type through Sidebar::set_bed_type_accord_combox, the public
// method Orca itself uses; it notifies the combo, so Orca's own handler owns
// the app-config write, the slice invalidation, and the re-render.
bool select_bed_type(Plater& plater, int bed_type_value);

// Opens an Orca settings tab, exactly as PlaterPresetComboBox::switch_to_tab.
void open_settings_tab(Preset::Type type);
// The filament settings of one slot, as the sidebar's own edit does.
void open_filament_settings(std::size_t slot);
// Orca's own "Add/Remove filaments" page, which lists every filament it ships.
// Returns once the page has closed.
void open_filament_library();
// Orca's own "Sync filaments" dialog, for the printer the project is set up
// for. Returns false when there is no connected printer to read.
bool sync_from_printer(Plater& plater);
// The monitor tab, when a machine is connected.
void open_monitor();

} // namespace Slic3r::GUI::JusPrin::SetupCommands
