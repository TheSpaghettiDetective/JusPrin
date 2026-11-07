#pragma once

// Bringing presets from an OrcaSlicer folder, item by item, for the Printers
// step's import result. OrcaSlicer's PresetBundle::import_presets does the
// importing; it only reports which files it imported, and asks yes or no when
// a name is taken. So each file is read here first: one that import_presets
// would skip gets its reason, one JusPrin already has is left alone, and one
// whose name JusPrin already has for another preset is imported from a
// renamed copy, "<name> (Orca copy)", written to a staging folder.
// OrcaSlicer's own files are only read.
//
// No wx and no Orca types: the preset collections are reached through the
// catalogue's callbacks.

#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterSetupTypes.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Home {

struct OrcaImportItem
{
    // By the folder the file is in: machine, filament or process.
    enum class Kind { Printer, Filament, Process };
    // AlreadyThere: JusPrin has this preset, from an earlier import or its
    // own, so there is nothing to bring.
    enum class Outcome { Pending, Imported, NotImported, AlreadyThere };
    // Why an item was not imported. import_presets skips a file it cannot
    // parse, one with no version, one of no known kind, and one whose base
    // profile it cannot find; NotLoaded is anything else it skipped.
    enum class Reason { None, Unreadable, NoVersion, NotAPreset, MissingBase, NotLoaded };

    Kind        kind{Kind::Printer};
    // The file in OrcaSlicer's folder, and the one handed to import_presets:
    // the same file, or its renamed copy.
    std::string source;
    std::string file;
    // The preset's own name, or the file's when it could not be read.
    std::string name;
    // The name it was imported under when JusPrin already had `name`.
    std::string renamed_to;
    Outcome     outcome{Outcome::Pending};
    Reason      reason{Reason::None};
    // The base profile it names, for MissingBase.
    std::string base;
    // A printer ticked in OrcaSlicer's printer list has no file: it is
    // installed from JusPrin's own profile for this vendor, model and nozzle.
    std::string vendor;
    std::string model;
    std::string nozzle;
};

struct OrcaImportCatalog
{
    // JusPrin already has a preset of this kind with this name.
    std::function<bool(OrcaImportItem::Kind, const std::string&)> has;
    // The "inherits" of JusPrin's preset of this kind with this name, which
    // it has; empty for a preset based on none.
    std::function<std::string(OrcaImportItem::Kind, const std::string&)> inherits;
    // The preset JusPrin loads a preset's "inherits" as, the name an
    // imported copy is saved inheriting; empty when JusPrin has none. It can
    // differ: OrcaSlicer falls back from a missing "Elegoo Generic ABS" to
    // its library's "Generic ABS @System".
    std::function<std::string(OrcaImportItem::Kind, const std::string&)> base;
};

// Reads each file and decides what import_presets gets: the items JusPrin
// already has are AlreadyThere (orca_preset_in_jusprin), the ones it would
// skip NotImported with their reason, the others Pending, renamed when their
// name is taken. A base profile among the files themselves counts as
// found, since import_presets loads the files in order. Renamed copies are
// written into `staging`, which must exist.
std::vector<OrcaImportItem> plan_orca_import(const std::vector<std::string>& files, const OrcaImportCatalog& catalog,
                                             const std::filesystem::path& staging);

// JusPrin has the preset in this file: one of the same kind with the same
// name, or with a name an earlier import renamed it to ("<name> (Orca
// copy)", "<name> (Orca copy 2)"), based on the same profile. A same-named
// preset based on another profile is another preset. False for a file that
// cannot be read.
bool orca_preset_in_jusprin(const std::string& file, const OrcaImportCatalog& catalog);

// Settles the Pending items once import_presets has run: `imported` is what
// it left in its `files` argument.
void settle_orca_import(std::vector<OrcaImportItem>& items, const std::vector<std::string>& imported);

// The presets an OrcaSlicer account folder holds (user/<account>): printers
// first, then filaments and processes, each sorted.
struct OrcaPresetFiles
{
    std::vector<std::string> files;
    std::vector<std::string> printers;
    std::vector<std::string> filaments;
    std::vector<std::string> processes;
};
OrcaPresetFiles scan_orca_presets(const std::filesystem::path& account_folder);

// The base profiles the printer presets among `files` name in their
// "inherits", such as "Bambu Lab A1 mini 0.4 nozzle", each once. A file is
// a printer preset by its folder, as plan_orca_import tells.
std::vector<std::string> orca_printer_bases(const std::vector<std::string>& files);

// What an import brings when the person leaves some printers unchosen:
// `printers` names the chosen ones as the import screen lists them, a saved
// printer by its file's name and a printer from JusPrin's own profile by the
// profile's name. Every filament and process file comes along whichever
// printers are chosen.
struct OrcaImportChoice
{
    std::vector<OrcaImportItem> profile_printers;
    std::vector<std::string>    files;
};
OrcaImportChoice orca_import_chosen(const OrcaPresetFiles& presets, const std::vector<OrcaImportItem>& profile_printers,
                                    const std::vector<std::string>& printers);

// A printer ticked in OrcaSlicer's printer list, as OrcaSlicer.conf's
// "models" records it: the vendor file's name, the model's profile name and
// the nozzle, as {"BBL", "Bambu Lab A1 mini", "0.4"}. Most people tick a
// printer and never save a preset of it.
struct OrcaTickedPrinter
{
    std::string vendor;
    std::string model;
    std::string nozzle;
};
// Nothing when OrcaSlicer.conf is missing or cannot be read.
std::vector<OrcaTickedPrinter> orca_ticked_printers(const std::filesystem::path& orca_root);

// The printer preset OrcaSlicer has selected, as OrcaSlicer.conf's
// "presets" "machine" names it: a system preset such as "Bambu Lab P1S 0.4
// nozzle", which need not be ticked, or a saved one. Empty when
// OrcaSlicer.conf is missing or names none.
std::string orca_selected_printer(const std::filesystem::path& orca_root);

// The printers to install from JusPrin's own profiles for an OrcaSlicer data
// folder: each one ticked, and the selected one, that a profile in
// `profiles` has, skipping any a saved preset in `account_folder` is based
// on, since that preset brings it. Each is an item named by its profile, as
// "Bambu Lab A1 mini 0.4 nozzle". `profiles` is called only when OrcaSlicer
// names a printer.
std::vector<OrcaImportItem> orca_profile_printers(
    const std::filesystem::path& orca_root, const std::optional<std::filesystem::path>& account_folder,
    const std::function<const std::vector<PrinterSetup::PrinterCandidate>&()>& profiles);

// The account folder inside OrcaSlicer's data folder: the one its
// OrcaSlicer.conf names, or, when it names none, the one whose printers were
// saved last. OrcaSlicer signed in to a Bambu account saves under that
// account's id, not under "default". Nothing when OrcaSlicer.conf cannot be
// read: importing from a guessed account would be guesswork.
std::optional<std::filesystem::path> orca_account_folder(const std::filesystem::path& orca_root);

// The account folder for a folder the person picked: OrcaSlicer's data
// folder, its user folder, an account folder, or an account's machine
// folder. Nothing when the folder is none of these.
std::optional<std::filesystem::path> orca_account_folder_for_pick(const std::filesystem::path& picked);

} // namespace Slic3r::GUI::JusPrin::Home
