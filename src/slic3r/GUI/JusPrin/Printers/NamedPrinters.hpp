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

} // namespace Slic3r::GUI::JusPrin::Printers
