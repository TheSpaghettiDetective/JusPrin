#pragma once

// Bringing an OrcaSlicer folder's printers into JusPrin, for the first
// launch's Printers step and for File › Import › Import from OrcaSlicer…:
// finding them, installing the ones ticked in OrcaSlicer from JusPrin's own
// profiles, and importing its saved presets through OrcaSlicer's
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

// The OrcaSlicer folder on offer: the one the person picked, or OrcaSlicer's
// own data folder beside JusPrin's. Its printers are the presets saved in its
// account folder, and the printers ticked in its printer list or selected in
// it that JusPrin ships a profile for, each of those an item of its own named
// by JusPrin's profile.
struct OrcaProfileSource
{
    // Empty when no folder was found.
    std::string                 root;
    OrcaPresetFiles             presets;
    std::vector<OrcaImportItem> ticked;
    // The printer preset OrcaSlicer has selected.
    std::string selected;

    bool                     has_printers() const { return !presets.printers.empty() || !ticked.empty(); }
    std::vector<std::string> printer_names() const;
};

// "printer", "filament" or "process", as an import's result names each kind.
const char* orca_import_kind(OrcaImportItem::Kind kind);
// Why an item was not imported, for the person, as a sentence that says
// what to do where there is something: "JusPrin couldn't read this file. To
// try again, choose File › Import › Import from OrcaSlicer…", or for a
// missing base "This filament is built on Kingroon PLA Silk, which JusPrin
// doesn't have. Pick a similar filament from the filament menu instead."
// Empty for one that was imported.
std::string orca_import_reason(const OrcaImportItem& item);

class OrcaImporter
{
public:
    explicit OrcaImporter(MainFrame& frame);
    ~OrcaImporter();

    // The folder `picked` names, otherwise OrcaSlicer's own beside JusPrin's
    // data folder. A ticked printer JusPrin already has is AlreadyThere.
    OrcaProfileSource detect(const std::optional<std::filesystem::path>& picked);

    // How each preset file is looked up among JusPrin's presets.
    OrcaImportCatalog catalog() const;

    // What the printer selection is left at once printers are brought. The
    // first launch selects the printer OrcaSlicer had selected, otherwise
    // the first one brought; the File menu leaves the open project's
    // printer, filaments and process as they were, and only adds printers.
    enum class Selection { Brought, Kept };

    // Brings `ticked` and `files` and says what became of each, ticked
    // printers last; `preferred` names the printer OrcaSlicer had selected.
    // Items already AlreadyThere stay as they are.
    std::vector<OrcaImportItem> bring(std::vector<OrcaImportItem> ticked, const std::vector<std::string>& files,
                                      const std::string& preferred, Selection selection);

    // The folder the person picked when asked for OrcaSlicer's: nothing when
    // they cancelled, a problem when it holds no OrcaSlicer printers.
    struct FolderChoice
    {
        std::optional<std::filesystem::path> folder;
        std::string                          problem;
    };
    FolderChoice choose_folder(wxWindow* parent);

    // Bringing these installs a printer profile, which reloads every preset
    // and with it drops unsaved changes to the ones the project uses.
    bool installs_profiles(const std::vector<OrcaImportItem>& ticked, const std::vector<std::string>& files);

    // JusPrin's own printer profiles, read the first time a ticked printer
    // needs matching.
    const PrinterSetup::PrinterCatalog& profile_catalog();

private:
    // Imports the saved presets, their bases installed already.
    std::vector<OrcaImportItem> import_files(const std::vector<std::string>& files);
    // The profile JusPrin installs for the base a saved printer names, when
    // it is not loaded yet and JusPrin ships one.
    const PrinterSetup::PrinterCandidate* missing_base(const std::string& base);
    // JusPrin's profile for a ticked printer.
    const PrinterSetup::PrinterCandidate& ticked_profile(const OrcaImportItem& item);
    // Saves each ticked printer, its profile installed already, as a
    // printer of its own; the name each was saved under, by item name.
    std::map<std::string, std::string> name_ticked(std::vector<OrcaImportItem>& items, bool installed);

    MainFrame&                                          m_frame;
    std::shared_ptr<const PrinterSetup::PrinterCatalog> m_profile_catalog;
};

} // namespace Slic3r::GUI::JusPrin::Home
