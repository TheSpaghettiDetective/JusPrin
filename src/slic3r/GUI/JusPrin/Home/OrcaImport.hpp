#pragma once

// Bringing presets from an OrcaSlicer folder, item by item, for the Printers
// step's import result. OrcaSlicer's PresetBundle::import_presets does the
// importing; it only reports which files it imported, and asks yes or no when
// a name is taken. So each file is read here first: one that import_presets
// would skip gets its reason, and one whose name JusPrin already has is
// imported from a renamed copy, "<name> (Orca copy)", written to a staging
// folder. OrcaSlicer's own files are only read.
//
// No wx and no Orca types: the preset collections are reached through the
// catalogue's callbacks.

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
    enum class Outcome { Pending, Imported, NotImported };
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
};

struct OrcaImportCatalog
{
    // JusPrin already has a preset of this kind with this name.
    std::function<bool(OrcaImportItem::Kind, const std::string&)> has;
    // The base profile a preset's "inherits" names is in JusPrin, as
    // import_presets looks it up.
    std::function<bool(OrcaImportItem::Kind, const std::string&)> has_base;
};

// Reads each file and decides what import_presets gets: the items it would
// skip are NotImported with their reason, the others Pending, renamed when
// their name is taken. A base profile among the files themselves counts as
// found, since import_presets loads the files in order. Renamed copies are
// written into `staging`, which must exist.
std::vector<OrcaImportItem> plan_orca_import(const std::vector<std::string>& files, const OrcaImportCatalog& catalog,
                                             const std::filesystem::path& staging);

// Settles the Pending items once import_presets has run: `imported` is what
// it left in its `files` argument.
void settle_orca_import(std::vector<OrcaImportItem>& items, const std::vector<std::string>& imported);

// The presets an OrcaSlicer account folder holds (user/<account>): printers
// first, then filaments and processes, each sorted, so a retry runs in the
// same order.
struct OrcaPresetFiles
{
    std::vector<std::string> files;
    std::vector<std::string> printers;
    std::vector<std::string> filaments;
    std::vector<std::string> processes;
};
OrcaPresetFiles scan_orca_presets(const std::filesystem::path& account_folder);

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
