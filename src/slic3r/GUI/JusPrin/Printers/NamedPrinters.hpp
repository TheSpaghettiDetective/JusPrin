#pragma once

// Named printers: one machine a person owns, with a name they chose.
//
// OrcaSlicer has no printer object. Its "Physical Printer" dialog saves the
// connection settings into a user printer profile under a name the person
// types, and the collection that once held physical printers is never loaded.
// So a named printer here *is* an Orca user printer profile: the profile name
// is the printer's name, two machines of the same model are two profiles that
// inherit the same system profile, and everything Orca does with profiles --
// the settings tab, the sidebar combo, cloud sync -- keeps working unchanged.
//
// Every write goes through the upstream entry point that already performs it
// (Tab::save_preset, Tab::select_preset, Tab::delete_preset), so dirty
// tracking, cloud sync, and the plater refresh are upstream's. The one thing
// Orca does not store is which Bambu device a printer was added from; that
// link lives in the app config under kDeviceSection, keyed by device id.

#include "InstalledModels.hpp"
#include "libslic3r/Preset.hpp"

#include <wx/string.h>

#include <string>
#include <vector>

namespace Slic3r::GUI { class Plater; }
namespace Slic3r::GUI::JusPrin::Workspace { class SpoolStore; }

namespace Slic3r::GUI::JusPrin::Printers {

struct NamedPrinter
{
    std::string name;      // the profile name, which is the printer's name
    std::string device_id; // the Bambu device it stands for; empty when none
    double      nozzle{0.};
    bool        selected{false};
};

// A visible user printer profile. Profiles embedded in a project, shipped in
// a bundle, or supplied by a vendor are not printers the person added.
bool is_named_printer(const Preset& preset);

// Every named printer, in the preset collection's order.
std::vector<NamedPrinter> named_printers();

// Saves the selected system printer profile as a new named printer and
// selects it. The name is `base_name`, or "base_name (2)" and so on when that
// is taken. `device_id`, when not empty, links the printer to that device; a
// device that already has a printer selects that printer instead. Returns the
// name of the printer now selected.
std::string add_named_printer(Plater& plater, const std::string& base_name, const std::string& device_id);

// Each returns an empty string when it did what was asked or when the person
// cancelled an Orca prompt along the way, and a message for the person when
// the request cannot be carried out (an invalid name, a printer that no
// longer exists). `spools` may be null.
wxString rename_named_printer(Plater& plater, Workspace::SpoolStore* spools, const std::string& from,
                              const std::string& to);
wxString remove_named_printer(Plater& plater, Workspace::SpoolStore* spools, const std::string& name);
wxString open_named_printer_settings(Plater& plater, const std::string& name);

// Moves a named printer onto `system_preset`, the sibling system profile for
// another nozzle size, keeping the person's own settings but not the ones the
// two nozzle profiles themselves disagree on -- those belong to the nozzle.
//
// A printer's nozzle is fixed by the profile it inherits, so changing it is a
// change of parent. It is made only where the person is editing that printer
// and the panel states the result; nothing else in JusPrin moves a parent.
// The open project's printer selection and its modified state stay as they
// were, and no dialog is shown. Returns a message for the person when it could
// not be done, empty when it was.
wxString change_named_printer_nozzle(Plater& plater, const std::string& name, const std::string& system_preset);

// After OrcaSlicer's own printer wizard has run: a named printer for each
// model it newly enabled, given the app config's vendor map from before it
// ran. The wizard's own selection stays selected, under its new name.
std::vector<std::string> name_installed_printers(Plater& plater, const VendorMap& before);

// Attach a device to an existing printer, without selecting or saving another
// profile. Both sides are unique; replacing a different association is explicit.
wxString link_named_printer(const std::string& name, const std::string& device_id);
wxString configure_named_printer_host(const std::string& name, PrintHostType type, const std::string& address,
                                     const std::string& api_key);

} // namespace Slic3r::GUI::JusPrin::Printers
