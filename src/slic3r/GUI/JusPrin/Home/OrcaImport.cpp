#include "OrcaImport.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace Slic3r::GUI::JusPrin::Home {

namespace fs = std::filesystem;

namespace {

// OrcaSlicer's own names (Preset.hpp, PresetBundle.hpp), repeated here so
// this file needs no libslic3r.
const char* const kUserFolder    = "user";     // PRESET_USER_DIR
const char* const kDefaultFolder = "default";  // DEFAULT_USER_FOLDER_NAME
const char* const kPrinterFolder = "machine";  // PRESET_PRINTER_NAME
const char* const kFilamentFolder = "filament"; // PRESET_FILAMENT_NAME
const char* const kProcessFolder = "process";  // PRESET_PRINT_NAME

using Kind = OrcaImportItem::Kind;

Kind kind_of_folder(const fs::path& file)
{
    const std::string folder = file.parent_path().filename().u8string();
    return folder == kFilamentFolder ? Kind::Filament : folder == kProcessFolder ? Kind::Process : Kind::Printer;
}

// The key import_json_presets tells the kinds apart by, which also holds
// the preset's name.
const char* settings_id_key(Kind kind)
{
    switch (kind) {
    case Kind::Filament: return "filament_settings_id";
    case Kind::Process: return "print_settings_id";
    case Kind::Printer: break;
    }
    return "printer_settings_id";
}

std::optional<nlohmann::json> read_json(const fs::path& file)
{
    std::ifstream stream(file);
    if (!stream)
        return std::nullopt;
    nlohmann::json json = nlohmann::json::parse(stream, nullptr, false);
    if (json.is_discarded() || !json.is_object())
        return std::nullopt;
    return json;
}

// OrcaSlicer.conf: JSON, then possibly a "# MD5 checksum" line, which reading
// one value leaves unread.
std::optional<nlohmann::json> read_config(const fs::path& file)
{
    std::ifstream stream(file);
    nlohmann::json json;
    try {
        stream >> json;
    } catch (const nlohmann::json::parse_error&) {
        return std::nullopt;
    }
    return json.is_object() ? std::optional<nlohmann::json>(std::move(json)) : std::nullopt;
}

std::string string_value(const nlohmann::json& json, const char* key)
{
    const auto found = json.find(key);
    return found != json.end() && found->is_string() ? found->get<std::string>() : std::string();
}

bool has_json_files(const fs::path& folder)
{
    std::error_code error;
    if (!fs::is_directory(folder, error))
        return false;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder, error))
        if (entry.path().extension() == ".json")
            return true;
    return false;
}

// The name an import gives the `copy`th renamed copy of `name`; the 0th is
// the name itself.
std::string copy_name(const std::string& name, int copy)
{
    if (copy == 0)
        return name;
    return name + (copy == 1 ? " (Orca copy)" : " (Orca copy " + std::to_string(copy) + ")");
}

// See orca_preset_in_jusprin. An import renames only a taken name, so the
// copies are looked for only while JusPrin has the name before them.
bool in_jusprin(Kind kind, const std::string& name, const std::string& base, const OrcaImportCatalog& catalog)
{
    // JusPrin's copy inherits the base as JusPrin loaded it.
    std::string loaded = base.empty() ? base : catalog.base(kind, base);
    if (loaded.empty())
        loaded = base;
    for (int copy = 0; catalog.has(kind, copy_name(name, copy)); ++copy)
        if (catalog.inherits(kind, copy_name(name, copy)) == loaded)
            return true;
    return false;
}

} // namespace

bool orca_preset_in_jusprin(const std::string& file, const OrcaImportCatalog& catalog)
{
    const fs::path path = fs::u8path(file);
    const std::optional<nlohmann::json> json = read_json(path);
    if (!json)
        return false;
    std::string name = string_value(*json, "name");
    if (name.empty())
        name = path.stem().u8string();
    return in_jusprin(kind_of_folder(path), name, string_value(*json, "inherits"), catalog);
}

std::vector<OrcaImportItem> plan_orca_import(const std::vector<std::string>& files, const OrcaImportCatalog& catalog,
                                             const fs::path& staging)
{
    std::vector<OrcaImportItem> items;
    // Names this import takes, so two renamed copies never share one.
    std::set<std::pair<Kind, std::string>> taken;
    std::vector<nlohmann::json> contents;
    for (const std::string& file : files) {
        OrcaImportItem item;
        item.source = item.file = file;
        const fs::path path     = fs::u8path(file);
        item.kind               = kind_of_folder(path);
        item.name               = path.stem().u8string();
        std::optional<nlohmann::json> json = read_json(path);
        if (!json) {
            item.outcome = OrcaImportItem::Outcome::NotImported;
            item.reason  = OrcaImportItem::Reason::Unreadable;
        } else {
            if (const std::string name = string_value(*json, "name"); !name.empty())
                item.name = name;
            const bool is_preset = json->contains("printer_settings_id") || json->contains("print_settings_id") ||
                                   json->contains("filament_settings_id");
            if (string_value(*json, "version").empty())
                item.reason = OrcaImportItem::Reason::NoVersion;
            else if (!is_preset)
                item.reason = OrcaImportItem::Reason::NotAPreset;
            if (item.reason != OrcaImportItem::Reason::None)
                item.outcome = OrcaImportItem::Outcome::NotImported;
            taken.insert({item.kind, item.name});
        }
        items.push_back(std::move(item));
        contents.push_back(json ? std::move(*json) : nlohmann::json());
    }

    for (size_t index = 0; index < items.size(); ++index) {
        OrcaImportItem& item = items[index];
        if (item.outcome != OrcaImportItem::Outcome::Pending)
            continue;
        nlohmann::json& json = contents[index];
        item.base            = string_value(json, "inherits");
        if (in_jusprin(item.kind, item.name, item.base, catalog)) {
            item.outcome = OrcaImportItem::Outcome::AlreadyThere;
            continue;
        }
        const bool in_batch  = std::any_of(items.begin(), items.end(), [&](const OrcaImportItem& other) {
            return &other != &item && other.kind == item.kind && other.name == item.base;
        });
        if (!item.base.empty() && !in_batch && catalog.base(item.kind, item.base).empty()) {
            item.outcome = OrcaImportItem::Outcome::NotImported;
            item.reason  = OrcaImportItem::Reason::MissingBase;
            continue;
        }
        if (!catalog.has(item.kind, item.name))
            continue;

        int         number  = 1;
        std::string renamed = copy_name(item.name, number);
        while (catalog.has(item.kind, renamed) || taken.count({item.kind, renamed}) != 0)
            renamed = copy_name(item.name, ++number);
        taken.insert({item.kind, renamed});
        json["name"]                    = renamed;
        json[settings_id_key(item.kind)] = renamed;
        // Numbered, not named: a preset name can hold characters a file name cannot.
        const fs::path copy = staging / (std::to_string(index) + ".json");
        std::ofstream  stream(copy);
        stream << json.dump(4);
        if (!stream)
            throw std::runtime_error("Could not write the renamed copy " + copy.u8string());
        item.file       = copy.u8string();
        item.renamed_to = renamed;
    }
    return items;
}

void settle_orca_import(std::vector<OrcaImportItem>& items, const std::vector<std::string>& imported)
{
    for (OrcaImportItem& item : items) {
        if (item.outcome != OrcaImportItem::Outcome::Pending)
            continue;
        if (std::find(imported.begin(), imported.end(), item.file) != imported.end()) {
            item.outcome = OrcaImportItem::Outcome::Imported;
        } else {
            item.outcome = OrcaImportItem::Outcome::NotImported;
            item.reason  = OrcaImportItem::Reason::NotLoaded;
        }
    }
}

OrcaPresetFiles scan_orca_presets(const fs::path& account_folder)
{
    OrcaPresetFiles presets;
    const auto scan = [&](const char* subfolder, std::vector<std::string>& names) {
        std::error_code error;
        const fs::path  folder = account_folder / subfolder;
        if (!fs::is_directory(folder, error))
            return;
        std::vector<fs::path> files;
        for (const fs::directory_entry& entry : fs::directory_iterator(folder, error))
            if (entry.is_regular_file(error) && entry.path().extension() == ".json")
                files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        for (const fs::path& file : files) {
            names.push_back(file.stem().u8string());
            presets.files.push_back(file.u8string());
        }
    };
    // Base machine profiles first, then their material and process children.
    scan(kPrinterFolder, presets.printers);
    scan(kFilamentFolder, presets.filaments);
    scan(kProcessFolder, presets.processes);
    return presets;
}

std::vector<std::string> orca_printer_bases(const std::vector<std::string>& files)
{
    std::vector<std::string> bases;
    for (const std::string& file : files) {
        const fs::path path = fs::u8path(file);
        if (kind_of_folder(path) != Kind::Printer)
            continue;
        if (const std::optional<nlohmann::json> json = read_config(path))
            if (std::string base = string_value(*json, "inherits");
                !base.empty() && std::find(bases.begin(), bases.end(), base) == bases.end())
                bases.push_back(std::move(base));
    }
    return bases;
}

OrcaImportChoice orca_import_chosen(const OrcaPresetFiles& presets, const std::vector<OrcaImportItem>& profile_printers,
                                    const std::vector<std::string>& printers)
{
    const auto chosen = [&printers](const std::string& name) { return std::find(printers.begin(), printers.end(), name) != printers.end(); };
    OrcaImportChoice choice;
    for (const OrcaImportItem& item : profile_printers)
        if (chosen(item.name))
            choice.profile_printers.push_back(item);
    // scan_orca_presets lists the printers' files first, in the order of their names.
    for (std::size_t index = 0; index < presets.files.size(); ++index)
        if (index >= presets.printers.size() || chosen(presets.printers[index]))
            choice.files.push_back(presets.files[index]);
    return choice;
}

std::vector<OrcaTickedPrinter> orca_ticked_printers(const fs::path& orca_root)
{
    std::vector<OrcaTickedPrinter> ticked;
    std::error_code error;
    const fs::path config = orca_root / "OrcaSlicer.conf";
    if (!fs::is_regular_file(config, error))
        return ticked;
    const std::optional<nlohmann::json> json = read_config(config);
    if (!json)
        return ticked;
    const auto models = json->find("models");
    if (models == json->end() || !models->is_array())
        return ticked;
    for (const nlohmann::json& entry : *models) {
        if (!entry.is_object())
            continue;
        const std::string vendor = string_value(entry, "vendor");
        const std::string model  = string_value(entry, "model");
        if (vendor.empty() || model.empty())
            continue;
        // The nozzles ticked for the model, as AppConfig writes them with
        // escape_strings_cstyle: "0.4", or "0.4;0.6" for two.
        std::string nozzles = string_value(entry, "nozzle_diameter");
        nozzles.erase(std::remove(nozzles.begin(), nozzles.end(), '"'), nozzles.end());
        for (std::size_t start = 0; start <= nozzles.size();) {
            const std::size_t end = std::min(nozzles.find(';', start), nozzles.size());
            if (end > start)
                ticked.push_back({vendor, model, nozzles.substr(start, end - start)});
            start = end + 1;
        }
    }
    return ticked;
}

std::string orca_selected_printer(const fs::path& orca_root)
{
    std::error_code error;
    const fs::path config = orca_root / "OrcaSlicer.conf";
    if (!fs::is_regular_file(config, error))
        return {};
    const std::optional<nlohmann::json> json = read_config(config);
    if (!json)
        return {};
    const auto presets = json->find("presets");
    return presets != json->end() && presets->is_object() ? string_value(*presets, "machine") : std::string();
}

std::vector<OrcaImportItem> orca_profile_printers(const fs::path& orca_root, const std::optional<fs::path>& account_folder,
                                                  const std::function<const std::vector<PrinterSetup::PrinterCandidate>&()>& profiles)
{
    using PrinterSetup::PrinterCandidate;
    std::vector<OrcaImportItem>    items;
    std::vector<OrcaTickedPrinter> named    = orca_ticked_printers(orca_root);
    const std::string              selected = orca_selected_printer(orca_root);
    if (named.empty() && selected.empty())
        return items;
    const std::vector<PrinterCandidate>& candidates = profiles();
    // The selected printer counts when it is one of JusPrin's system printers.
    for (const PrinterCandidate& candidate : candidates)
        if (!selected.empty() && candidate.preset_name == selected) {
            named.push_back({candidate.vendor_id, candidate.model_id, candidate.variant});
            break;
        }
    const std::vector<std::string> bases =
        account_folder ? orca_printer_bases(scan_orca_presets(*account_folder).files) : std::vector<std::string>();
    for (const OrcaTickedPrinter& printer : named) {
        const auto profile = std::find_if(candidates.begin(), candidates.end(), [&printer](const PrinterCandidate& candidate) {
            return candidate.vendor_id == printer.vendor && candidate.model_id == printer.model && candidate.variant == printer.nozzle;
        });
        if (profile == candidates.end() || std::find(bases.begin(), bases.end(), profile->preset_name) != bases.end() ||
            std::any_of(items.begin(), items.end(), [&](const OrcaImportItem& item) { return item.name == profile->preset_name; }))
            continue;
        OrcaImportItem item;
        item.name   = profile->preset_name;
        item.vendor = printer.vendor;
        item.model  = printer.model;
        item.nozzle = printer.nozzle;
        items.push_back(std::move(item));
    }
    return items;
}

std::optional<fs::path> orca_account_folder(const fs::path& orca_root)
{
    std::string configured;
    const fs::path config = orca_root / "OrcaSlicer.conf";
    std::error_code error;
    if (fs::is_regular_file(config, error)) {
        std::optional<nlohmann::json> json = read_config(config);
        if (!json)
            return std::nullopt;
        const auto app = json->find("app");
        if (app != json->end() && app->is_object())
            configured = string_value(*app, "preset_folder");
    }

    const fs::path user = orca_root / kUserFolder;
    if (!configured.empty())
        return user / configured;
    fs::path        newest = user / kDefaultFolder;
    fs::file_time_type written{};
    for (const fs::directory_entry& account : fs::directory_iterator(user, error)) {
        for (const fs::directory_entry& item : fs::directory_iterator(account.path() / kPrinterFolder, error)) {
            if (item.path().extension() != ".json")
                continue;
            if (const fs::file_time_type time = item.last_write_time(error); time > written) {
                written = time;
                newest  = account.path();
            }
        }
        error.clear();
    }
    return newest;
}

std::optional<fs::path> orca_account_folder_for_pick(const fs::path& picked)
{
    std::error_code error;
    if (fs::is_directory(picked / kUserFolder, error))
        return orca_account_folder(picked);
    if (has_json_files(picked / kPrinterFolder) || has_json_files(picked / kFilamentFolder) ||
        has_json_files(picked / kProcessFolder))
        return picked;
    if (picked.filename() == kPrinterFolder && has_json_files(picked))
        return picked.parent_path();
    // The user folder: its account folders hold the presets.
    if (picked.filename() == kUserFolder)
        return orca_account_folder(picked.parent_path());
    return std::nullopt;
}

} // namespace Slic3r::GUI::JusPrin::Home
