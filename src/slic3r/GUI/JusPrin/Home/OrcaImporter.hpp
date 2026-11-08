#pragma once

// Bringing the printers of an OrcaSlicer folder, or of a JusPrin 1 folder,
// into JusPrin, for the first launch's Printers step and for File › Import ›
// Import from OrcaSlicer… and Import from JusPrin 1…: finding them,
// installing the ones ticked in the other app from JusPrin's own profiles,
// and importing its saved presets through OrcaSlicer's
// PresetBundle::import_presets. OrcaImport.hpp reads the files.

#include "OrcaImport.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class wxWindow;

namespace Slic3r::GUI {
class MainFrame;
}
namespace Slic3r::GUI::JusPrin::PrinterSetup {
class PrinterCatalog;
struct PrinterCandidate;
}

namespace Slic3r::GUI::JusPrin::Home {

// The folder on offer: the one the person picked, or the app's own data
// folder on this computer (import_app_folder). Its printers are the presets saved in its
// account folder, and the printers ticked in its printer list or selected in
// it that JusPrin ships a profile for, each of those an item of its own named
// by JusPrin's profile.
struct OrcaProfileSource
{
    const ImportApp* app{&kOrcaSlicer};
    // Empty when no folder was found.
    std::string                 root;
    OrcaPresetFiles             presets;
    std::vector<OrcaImportItem> ticked;
    // The printer preset the app has selected.
    std::string selected;

    bool                     has_printers() const { return !presets.printers.empty() || !ticked.empty(); }
    std::vector<std::string> printer_names() const;
};

// OrcaSlicer's data folder on this computer, whatever folder JusPrin was
// started with: the first of orca_data_folders() that is there, otherwise
// OrcaSlicer's default for the platform. JUSPRIN_ORCA_DATADIR names another
// folder, and set to "" means none: test runs set it so they never read the
// person's real OrcaSlicer.
std::filesystem::path orca_folder();
// orca_folder(), or JusPrin 1's (V1Import::v1_folder()).
std::filesystem::path import_app_folder(const ImportApp& app);

// "printer", "filament" or "process", as an import's result names each kind.
const char* orca_import_kind(OrcaImportItem::Kind kind);
// Why an item was not imported, for the person, as a sentence that says
// what to do where there is something: "JusPrin couldn't read this file. To
// try again, choose File › Import › Import from OrcaSlicer…" (the menu item
// of the app it came from), or for a
// missing base "Not imported, because it's based on Kingroon PLA Silk, which
// JusPrin doesn't have. It stays in OrcaSlicer. When you prepare a print,
// choose a similar filament instead."
// Empty for one that was imported.
std::string orca_import_reason(const ImportApp& app, const OrcaImportItem& item);
// The same, under the item's name in a result's details, where the line
// above already says what wasn't imported and that it stays in the app: for
// a missing base "It's based on Kingroon PLA Silk, which JusPrin doesn't
// have. When you prepare a print, choose a similar filament instead."
std::string orca_import_detail(const ImportApp& app, const OrcaImportItem& item);

class OrcaImporter
{
public:
    explicit OrcaImporter(MainFrame& frame);
    ~OrcaImporter();

    // The folder `picked` names, otherwise import_app_folder(app). A ticked
    // printer JusPrin already has is AlreadyThere.
    OrcaProfileSource detect(const ImportApp& app, const std::optional<std::filesystem::path>& picked);

    // How each preset file is looked up among JusPrin's presets.
    OrcaImportCatalog catalog() const;

    // What the printer selection is left at once printers are brought. The
    // first launch selects the printer the app had selected, otherwise
    // the first one brought; the File menu leaves the open project's
    // printer, filaments and process as they were, and only adds printers.
    enum class Selection { Brought, Kept };

    // Brings `ticked` and `files` from `source` and says what became of
    // each, ticked printers last. Items already AlreadyThere stay as they
    // are. From JusPrin 1 it also copies Bambu's network file
    // (V1Import::copy_network_settings).
    std::vector<OrcaImportItem> bring(const OrcaProfileSource& source, std::vector<OrcaImportItem> ticked,
                                      const std::vector<std::string>& files, Selection selection);

    // The folder the person picked when asked for the app's: nothing when
    // they cancelled, a problem when it holds none of the app's printers.
    struct FolderChoice
    {
        std::optional<std::filesystem::path> folder;
        std::string                          problem;
    };
    FolderChoice choose_folder(const ImportApp& app, wxWindow* parent);

    // Bringing these installs a printer profile, which reloads every preset
    // and with it drops unsaved changes to the ones the project uses.
    bool installs_profiles(const ImportApp& app, const std::vector<OrcaImportItem>& ticked, const std::vector<std::string>& files);

    // JusPrin's own printer profiles, read the first time a ticked printer
    // needs matching.
    const PrinterSetup::PrinterCatalog& profile_catalog();
    // The profile JusPrin installs for the base a saved printer names, when
    // it is not loaded yet and JusPrin ships one.
    const PrinterSetup::PrinterCandidate* missing_base(const std::string& base);

private:
    // Imports the saved presets, their bases installed already.
    std::vector<OrcaImportItem> import_files(const ImportApp& app, const std::vector<std::string>& files);
    // JusPrin's profile for a ticked printer.
    const PrinterSetup::PrinterCandidate& ticked_profile(const OrcaImportItem& item);
    // Saves each ticked printer, its profile installed already, as a
    // printer of its own; the name each was saved under, by item name.
    std::map<std::string, std::string> name_ticked(std::vector<OrcaImportItem>& items, bool installed);

    MainFrame&                                          m_frame;
    std::shared_ptr<const PrinterSetup::PrinterCatalog> m_profile_catalog;
};

} // namespace Slic3r::GUI::JusPrin::Home
