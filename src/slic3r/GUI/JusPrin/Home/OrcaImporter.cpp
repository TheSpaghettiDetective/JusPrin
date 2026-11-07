// First: slic3r/GUI/I18N.hpp defines _L only while the _ macro is still
// undefined, so it has to precede any header that brings one in.
#include "slic3r/GUI/I18N.hpp"

#include "OrcaImporter.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Printers/NamedPrinters.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterCatalog.hpp"
#include "slic3r/GUI/JusPrin/Shell/SetupCommands.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <boost/log/trivial.hpp>
#include <wx/dirdlg.h>
#include <wx/utils.h>

#include <algorithm>
#include <map>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin::Home {

namespace fs = std::filesystem;

namespace {

PresetCollection& preset_collection(PresetBundle& presets, OrcaImportItem::Kind kind)
{
    switch (kind) {
    case OrcaImportItem::Kind::Filament: return presets.filaments;
    case OrcaImportItem::Kind::Process: return presets.prints;
    case OrcaImportItem::Kind::Printer: break;
    }
    return presets.printers;
}

PresetBundle& preset_bundle()
{
    if (wxGetApp().preset_bundle == nullptr || wxGetApp().plater() == nullptr)
        throw std::logic_error("An OrcaSlicer import ran before the app finished starting");
    return *wxGetApp().preset_bundle;
}

// JusPrin has a printer of this profile: a named printer based on it, as an
// earlier import or the person added it.
bool has_named_printer_of(const PresetBundle& presets, const std::string& profile)
{
    for (const Printers::NamedPrinter& printer : Printers::named_printers())
        if (const Preset* preset = presets.printers.find_preset(printer.name, false); preset != nullptr && preset->inherits() == profile)
            return true;
    return false;
}

} // namespace

const char* orca_import_kind(OrcaImportItem::Kind kind)
{
    switch (kind) {
    case OrcaImportItem::Kind::Filament: return "filament";
    case OrcaImportItem::Kind::Process: return "process";
    case OrcaImportItem::Kind::Printer: break;
    }
    return "printer";
}

std::string orca_import_reason(const OrcaImportItem& item)
{
    using Reason = OrcaImportItem::Reason;
    const wxString base = wxString::FromUTF8(item.base);
    switch (item.reason) {
    // These two can go another way next time, so they say how to try again.
    case Reason::Unreadable:
        return into_u8(_L("JusPrin couldn't read this file. To try again, choose File › Import › Import from OrcaSlicer…"));
    case Reason::NoVersion: return into_u8(_L("Not imported. The file has no version."));
    case Reason::NotAPreset: return into_u8(_L("Not imported. It isn't a printer, filament or process preset."));
    case Reason::MissingBase:
        switch (item.kind) {
        case OrcaImportItem::Kind::Filament:
            return into_u8(wxString::Format(
                _L("This filament is built on %s, which JusPrin doesn't have. Pick a similar filament from the filament menu instead."),
                base));
        case OrcaImportItem::Kind::Process:
            return into_u8(wxString::Format(_L("These settings are built on %s, which JusPrin doesn't have."), base));
        case OrcaImportItem::Kind::Printer: break;
        }
        return into_u8(wxString::Format(_L("This printer is built on %s, which JusPrin doesn't have."), base));
    case Reason::NotLoaded:
        return into_u8(_L("JusPrin couldn't load this preset. To try again, choose File › Import › Import from OrcaSlicer…"));
    case Reason::None: break;
    }
    return {};
}

std::vector<std::string> OrcaProfileSource::printer_names() const
{
    std::vector<std::string> names = presets.printers;
    for (const OrcaImportItem& item : ticked)
        names.push_back(item.name);
    return names;
}

OrcaImporter::OrcaImporter(MainFrame& frame) : m_frame(frame) {}

OrcaImporter::~OrcaImporter() = default;

OrcaProfileSource OrcaImporter::detect(const std::optional<fs::path>& picked)
{
    OrcaProfileSource       source;
    std::optional<fs::path> account;
    fs::path                root;
    if (picked) {
        source.root = picked->u8string();
        root        = *picked;
        account     = orca_account_folder_for_pick(*picked);
    } else {
        const fs::path data = fs::u8path(data_dir());
        root                = data.parent_path() / "OrcaSlicer";
        std::error_code error;
        if (!fs::is_directory(root, error) || root == data)
            return source;
        source.root = root.u8string();
        account     = orca_account_folder(root);
    }
    if (account)
        source.presets = scan_orca_presets(*account);
    source.selected = orca_selected_printer(root);
    source.ticked   = orca_profile_printers(root, account, [this]() -> const auto& { return profile_catalog().candidates(); });
    if (!source.ticked.empty()) {
        const PresetBundle& presets = preset_bundle();
        for (OrcaImportItem& item : source.ticked)
            if (has_named_printer_of(presets, item.name))
                item.outcome = OrcaImportItem::Outcome::AlreadyThere;
    }
    return source;
}

OrcaImportCatalog OrcaImporter::catalog() const
{
    PresetBundle*     presets = &preset_bundle();
    OrcaImportCatalog catalog;
    catalog.has = [presets](OrcaImportItem::Kind kind, const std::string& name) {
        return preset_collection(*presets, kind).find_preset(name, false) != nullptr;
    };
    catalog.inherits = [presets](OrcaImportItem::Kind kind, const std::string& name) {
        return preset_collection(*presets, kind).find_preset(name, false)->inherits();
    };
    catalog.base = [presets](OrcaImportItem::Kind kind, const std::string& base) {
        // As import_presets looks the base up.
        const Preset* loaded = preset_collection(*presets, kind).find_preset2(base, true);
        return loaded != nullptr ? loaded->name : std::string();
    };
    return catalog;
}

std::vector<OrcaImportItem> OrcaImporter::bring(std::vector<OrcaImportItem> ticked, const std::vector<std::string>& files,
                                                const std::string& preferred, Selection selection)
{
    // Naming a printer selects it, so the project's choices are put back
    // afterwards.
    PresetBundle&                  presets        = preset_bundle();
    Plater&                        plater         = *wxGetApp().plater();
    const std::string              kept_printer   = presets.printers.get_selected_preset_name();
    const std::vector<std::string> kept_filaments = presets.filament_presets;
    const std::string              kept_process   = presets.prints.get_selected_preset_name();

    // Every profile the import needs goes in with one preset reload, which
    // re-reads every installed vendor's profiles and takes seconds: the
    // ticked printers' own, and the bases saved printers name. A saved
    // printer imports only once its base is loaded, and JusPrin loads a
    // vendor's profiles only once one of its printers is installed, which on
    // a fresh install none is. A base that fails to install stays missing,
    // and its printer's line says so.
    std::vector<SetupCommands::PrinterProfile> profiles;
    for (const OrcaImportItem& item : ticked)
        if (item.outcome != OrcaImportItem::Outcome::AlreadyThere)
            profiles.push_back({item.vendor, item.model, item.nozzle, ticked_profile(item).default_material});
    for (const std::string& base : orca_printer_bases(files))
        if (const PrinterSetup::PrinterCandidate* profile = missing_base(base))
            profiles.push_back({profile->vendor_id, profile->model_id, profile->variant, profile->default_material});
    std::string problem;
    const bool  installed = SetupCommands::install_printer_profiles(plater, profiles, problem);
    if (!installed)
        BOOST_LOG_TRIVIAL(error) << "Installing the profiles of OrcaSlicer's printers failed: " << problem;

    std::vector<OrcaImportItem>        items = import_files(files);
    const std::map<std::string, std::string> saved = name_ticked(ticked, installed);
    items.insert(items.end(), std::make_move_iterator(ticked.begin()), std::make_move_iterator(ticked.end()));

    if (selection == Selection::Kept) {
        if (presets.printers.get_selected_preset_name() != kept_printer && !SetupCommands::select_printer_preset(plater, kept_printer))
            throw std::logic_error("The project's printer " + kept_printer + " was gone after an OrcaSlicer import");
        for (std::size_t slot = 0; slot < kept_filaments.size() && slot < presets.filament_presets.size(); ++slot)
            if (presets.filament_presets[slot] != kept_filaments[slot])
                SetupCommands::select_filament_preset(plater, slot, kept_filaments[slot]);
        if (presets.prints.get_selected_preset_name() != kept_process)
            SetupCommands::select_process_preset(plater, kept_process);
        return items;
    }
    // The printer OrcaSlicer had selected, otherwise the first one brought.
    const auto saved_as = [&saved](const OrcaImportItem& item) {
        if (!item.vendor.empty())
            return saved.at(item.name);
        return item.renamed_to.empty() ? item.name : item.renamed_to;
    };
    const auto is_printer = [](const OrcaImportItem& item) {
        return item.kind == OrcaImportItem::Kind::Printer && item.outcome == OrcaImportItem::Outcome::Imported;
    };
    auto chosen = std::find_if(items.begin(), items.end(), [&](const OrcaImportItem& item) { return is_printer(item) && item.name == preferred; });
    if (chosen == items.end())
        chosen = std::find_if(items.begin(), items.end(), is_printer);
    if (chosen != items.end())
        SetupCommands::select_printer_preset(plater, saved_as(*chosen));
    return items;
}

OrcaImporter::FolderChoice OrcaImporter::choose_folder(wxWindow* parent)
{
    wxDirDialog dialog(parent, _L("Choose your OrcaSlicer folder"), wxEmptyString, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
        return {};
    const fs::path picked = fs::u8path(into_u8(dialog.GetPath()));
    if (!detect(picked).has_printers())
        return {std::nullopt, into_u8(_L("JusPrin found no OrcaSlicer printers in that folder."))};
    return {picked, {}};
}

bool OrcaImporter::installs_profiles(const std::vector<OrcaImportItem>& ticked, const std::vector<std::string>& files)
{
    if (std::any_of(ticked.begin(), ticked.end(), [](const OrcaImportItem& item) { return item.outcome != OrcaImportItem::Outcome::AlreadyThere; }))
        return true;
    const OrcaImportCatalog lookup = catalog();
    std::vector<std::string> wanted;
    for (const std::string& file : files)
        if (!orca_preset_in_jusprin(file, lookup))
            wanted.push_back(file);
    const std::vector<std::string> bases = orca_printer_bases(wanted);
    return std::any_of(bases.begin(), bases.end(), [this](const std::string& base) { return missing_base(base) != nullptr; });
}

const PrinterSetup::PrinterCandidate* OrcaImporter::missing_base(const std::string& base)
{
    if (preset_bundle().printers.find_preset2(base, true) != nullptr)
        return nullptr;
    const std::vector<PrinterSetup::PrinterCandidate>& candidates = profile_catalog().candidates();
    const auto profile = std::find_if(candidates.begin(), candidates.end(),
                                      [&base](const PrinterSetup::PrinterCandidate& candidate) { return candidate.preset_name == base; });
    return profile == candidates.end() ? nullptr : &*profile;
}

const PrinterSetup::PrinterCatalog& OrcaImporter::profile_catalog()
{
    if (!m_profile_catalog)
        m_profile_catalog = std::make_shared<const PrinterSetup::PrinterCatalog>(PrinterSetup::PrinterCatalog::load(Slic3r::resources_dir()));
    return *m_profile_catalog;
}

const PrinterSetup::PrinterCandidate& OrcaImporter::ticked_profile(const OrcaImportItem& item)
{
    const std::vector<PrinterSetup::PrinterCandidate>& candidates = profile_catalog().candidates();
    const auto profile = std::find_if(candidates.begin(), candidates.end(), [&item](const PrinterSetup::PrinterCandidate& candidate) {
        return candidate.vendor_id == item.vendor && candidate.model_id == item.model && candidate.variant == item.nozzle;
    });
    if (profile == candidates.end())
        throw std::logic_error("A printer ticked in OrcaSlicer lost its JusPrin profile between detection and import");
    return *profile;
}

std::map<std::string, std::string> OrcaImporter::name_ticked(std::vector<OrcaImportItem>& items, bool installed)
{
    Plater&                            plater  = *wxGetApp().plater();
    PresetBundle&                      presets = preset_bundle();
    std::map<std::string, std::string> saved;
    // Each printer is saved under its model's name, two nozzles of one model
    // as two printers. The reason for a failure is the generic one:
    // installing a shipped profile fails only on a broken install, which the
    // log describes.
    for (OrcaImportItem& item : items) {
        if (item.outcome == OrcaImportItem::Outcome::AlreadyThere)
            continue;
        const Preset* profile = installed ? presets.printers.find_system_preset_by_model_and_variant(item.model, item.nozzle) : nullptr;
        if (profile != nullptr && SetupCommands::select_printer_preset(plater, profile->name)) {
            saved[item.name] = Printers::add_named_printer(plater, item.model, {});
            item.outcome     = OrcaImportItem::Outcome::Imported;
            item.reason      = OrcaImportItem::Reason::None;
        } else {
            BOOST_LOG_TRIVIAL(error) << "Selecting " << item.model << " from OrcaSlicer's printer list failed after its profile was "
                                     << (installed ? "installed" : "not installed");
            item.outcome = OrcaImportItem::Outcome::NotImported;
            item.reason  = OrcaImportItem::Reason::NotLoaded;
        }
    }
    return saved;
}

std::vector<OrcaImportItem> OrcaImporter::import_files(const std::vector<std::string>& files)
{
    PresetBundle&           presets = preset_bundle();
    AppConfig&              config  = *wxGetApp().app_config;
    const OrcaImportCatalog lookup  = catalog();

    // Renamed copies live here only until import_presets has copied them.
    const fs::path staging = fs::temp_directory_path() / ("jusprin-orca-import-" + std::to_string(wxGetProcessId()));
    fs::remove_all(staging);
    fs::create_directories(staging);
    std::vector<OrcaImportItem> items = plan_orca_import(files, lookup, staging);

    std::vector<std::string> pending;
    for (const OrcaImportItem& item : items)
        if (item.outcome == OrcaImportItem::Outcome::Pending)
            pending.push_back(item.file);
    if (!pending.empty()) {
        // Every taken name was renamed above, so import_presets has nothing
        // to ask. Were it asked, the answer is no and that item says it was
        // not imported.
        presets.import_presets(pending, [](const std::string&) { return 0; }, ForwardCompatibilitySubstitutionRule::Enable, config);
        wxGetApp().load_current_presets();
        presets.update_compatible(PresetSelectCompatibleType::Always);
        m_frame.update_side_preset_ui();
    }
    // import_presets leaves in `pending` what it imported.
    settle_orca_import(items, pending);
    fs::remove_all(staging);
    return items;
}

} // namespace Slic3r::GUI::JusPrin::Home
