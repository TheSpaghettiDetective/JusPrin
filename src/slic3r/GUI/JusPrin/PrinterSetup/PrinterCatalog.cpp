#include "PrinterCatalog.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

json read_json(const fs::path& path)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Could not read printer profile: " + path.string());
    return json::parse(input);
}

std::vector<std::string> split(const std::string& value, char delimiter)
{
    std::vector<std::string> result;
    std::istringstream input(value);
    std::string part;
    while (std::getline(input, part, delimiter))
        if (!part.empty()) result.push_back(part);
    return result;
}

std::string first_string(const json& value)
{
    if (value.is_string()) return value.get<std::string>();
    if (value.is_array() && !value.empty() && value.front().is_string())
        return value.front().get<std::string>();
    return {};
}

json resolve_machine(const std::string& name, const std::map<std::string, json>& machines,
                     std::set<std::string>& visiting)
{
    const auto found = machines.find(name);
    if (found == machines.end() || !visiting.insert(name).second)
        return json::object();
    json resolved = json::object();
    const std::string parent = found->second.value("inherits", "");
    if (!parent.empty())
        resolved = resolve_machine(parent, machines, visiting);
    for (const auto& item : found->second.items())
        resolved[item.key()] = item.value();
    visiting.erase(name);
    return resolved;
}

std::string build_volume(const json& machine)
{
    const auto area = machine.value("printable_area", json::array());
    double min_x = 0., min_y = 0., max_x = 0., max_y = 0.;
    bool first = true;
    for (const auto& point : area) {
        if (!point.is_string()) continue;
        const auto values = split(point.get<std::string>(), 'x');
        if (values.size() != 2) continue;
        try {
            const double x = std::stod(values[0]);
            const double y = std::stod(values[1]);
            if (first) { min_x = max_x = x; min_y = max_y = y; first = false; }
            else { min_x = std::min(min_x, x); max_x = std::max(max_x, x);
                   min_y = std::min(min_y, y); max_y = std::max(max_y, y); }
        } catch (const std::exception&) {}
    }
    if (first) return {};
    const std::string height = first_string(machine.value("printable_height", json()));
    if (height.empty()) return {};
    auto clean = [](double value) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(value == std::floor(value) ? 0 : 1) << value;
        return out.str();
    };
    return clean(max_x - min_x) + " × " + clean(max_y - min_y) + " × " + height + " mm";
}

} // namespace

PrinterCatalog::PrinterCatalog(std::vector<PrinterCandidate> candidates) : m_candidates(std::move(candidates)) {}

PrinterCatalog PrinterCatalog::load(const std::string& resources_directory)
{
    const fs::path profiles = fs::path(resources_directory) / "profiles";
    std::vector<PrinterCandidate> candidates;
    for (const fs::directory_entry& vendor_entry : fs::directory_iterator(profiles)) {
        if (!vendor_entry.is_regular_file() || vendor_entry.path().extension() != ".json") continue;
        const std::string vendor_id = vendor_entry.path().stem().string();
        const json index = read_json(vendor_entry.path());
        if (!index.contains("machine_model_list") || !index.contains("machine_list")) continue;
        const std::string vendor_name = index.value("name", vendor_id);
        const fs::path vendor_dir = profiles / vendor_id;

        std::map<std::string, json> machines;
        for (const auto& entry : index["machine_list"]) {
            const std::string name = entry.value("name", "");
            const std::string sub_path = entry.value("sub_path", "");
            if (!name.empty() && !sub_path.empty()) machines.emplace(name, read_json(vendor_dir / sub_path));
        }

        // Which machine profile is a model's profile for a nozzle size, by
        // what each profile declares rather than by its name: vendors name
        // them "K1 (0.4 nozzle)", "MINIIS 0.4 nozzle" and more.
        std::map<std::pair<std::string, std::string>, std::string> profile_of;
        for (const auto& [name, raw] : machines) {
            std::set<std::string> visiting;
            const json resolved = resolve_machine(name, machines, visiting);
            if (resolved.value("instantiation", "true") == "false") continue;
            const std::string printer_model = first_string(resolved.value("printer_model", json()));
            const std::string printer_variant = first_string(resolved.value("printer_variant", json()));
            if (!printer_model.empty() && !printer_variant.empty())
                profile_of.emplace(std::make_pair(printer_model, printer_variant), name);
        }

        for (const auto& entry : index["machine_model_list"]) {
            const std::string sub_path = entry.value("sub_path", "");
            if (sub_path.empty()) continue;
            const json model = read_json(vendor_dir / sub_path);
            const std::string model_name = model.value("name", entry.value("name", ""));
            const std::string model_id = model_name;
            const std::string device_model = model.value("model_id", "");
            const auto variants = split(first_string(model.value("nozzle_diameter", json())), ';');
            const std::string plate = first_string(model.value("default_bed_type", json()));
            fs::path artwork = vendor_dir / (model_name + "_cover.png");
            if (!fs::exists(artwork)) artwork.clear();

            for (const std::string& variant : variants) {
                const auto        declared    = profile_of.find(std::make_pair(model_name, variant));
                const std::string preset_name = declared != profile_of.end() ? declared->second :
                                                                              model_name + " " + variant + " nozzle";
                std::set<std::string> visiting;
                const json machine = resolve_machine(preset_name, machines, visiting);
                if (machine.empty()) continue; // not an installable variant
                PrinterCandidate candidate;
                candidate.id = "printer-" + std::to_string(candidates.size());
                candidate.vendor_id = vendor_id;
                candidate.vendor_name = vendor_name;
                candidate.model_id = model_id;
                candidate.model_name = model_name;
                candidate.device_model_id = device_model;
                candidate.variant = variant;
                candidate.preset_name = preset_name;
                candidate.build_volume = build_volume(machine);
                // OrcaSlicer's own default for this machine profile. Not the
                // model's default_materials: that is the wizard's list of
                // filaments to tick, and its first entry is often not PLA.
                candidate.default_material = first_string(machine.value("default_filament_profile", json()));
                candidate.default_plate = plate;
                candidate.artwork_path = artwork.string();
                candidates.push_back(std::move(candidate));
            }
        }
    }
    if (candidates.empty())
        throw std::runtime_error("No installable printer profiles were found.");
    return PrinterCatalog(std::move(candidates));
}

const PrinterCandidate* PrinterCatalog::find(const std::string& id) const
{
    const auto found = std::find_if(m_candidates.begin(), m_candidates.end(),
                                    [&](const PrinterCandidate& item) { return item.id == id; });
    return found == m_candidates.end() ? nullptr : &*found;
}

const PrinterCandidate* PrinterCatalog::find_device_model(const std::string& device_model_id) const
{
    const auto found = std::find_if(m_candidates.begin(), m_candidates.end(), [&](const PrinterCandidate& item) {
        return !device_model_id.empty() && item.device_model_id == device_model_id && item.variant == "0.4";
    });
    if (found != m_candidates.end()) return &*found;
    const auto any = std::find_if(m_candidates.begin(), m_candidates.end(), [&](const PrinterCandidate& item) {
        return !device_model_id.empty() && item.device_model_id == device_model_id;
    });
    return any == m_candidates.end() ? nullptr : &*any;
}

namespace {

// Hidden from the printer panel only, decided 2026-09-17: the bundle stays
// in OrcaSlicer's own wizard.
constexpr const char* kPanelHiddenVendor = "OrcaArena";

std::string letters_and_digits(const std::string& text)
{
    std::string kept;
    for (unsigned char letter : text)
        if (std::isalnum(letter))
            kept.push_back(static_cast<char>(std::tolower(letter)));
    return kept;
}

double nozzle_of(const std::string& variant)
{
    try {
        return std::stod(variant);
    } catch (const std::exception&) {
        return 0.;
    }
}

// Most profiles repeat the brand at the start of the model's name, often
// spelled better than the vendor file does ("Bambu Lab A1 mini" under
// "Bambulab"). Then the brand is that spelling and the model is the rest;
// otherwise the brand is the vendor file's own name.
void name_printer(CatalogPrinter& printer, const std::string& vendor_name, const std::string& profile_name)
{
    printer.vendor_name     = vendor_name;
    printer.model_name      = profile_name;
    const std::string brand = letters_and_digits(vendor_name);
    // At each word boundary, "Bambu" then "Bambu Lab": the brand may be more
    // than one word.
    for (std::size_t end = profile_name.find(' '); end != std::string::npos && !brand.empty();
         end = profile_name.find(' ', end + 1)) {
        const std::string prefix = letters_and_digits(profile_name.substr(0, end));
        if (prefix.size() > brand.size())
            break;
        if (prefix == brand) {
            printer.vendor_name = profile_name.substr(0, end);
            printer.model_name  = profile_name.substr(end + 1);
            break;
        }
    }
}

} // namespace

std::vector<CatalogPrinter> PrinterCatalog::panel_printers() const
{
    // The catalogue carries one candidate per model and nozzle; a person has
    // one printer with one nozzle, so the variants fold into the model and
    // the sizes it ships become a fact about it.
    std::vector<CatalogPrinter> printers;
    for (const PrinterCandidate& candidate : m_candidates) {
        if (candidate.vendor_id == kPanelHiddenVendor)
            continue;
        const std::string id    = candidate.vendor_id + "/" + candidate.model_id;
        const auto  known = std::find_if(printers.begin(), printers.end(),
                                         [&id](const CatalogPrinter& printer) { return printer.id == id; });
        std::size_t index = static_cast<std::size_t>(known - printers.begin());
        if (known == printers.end()) {
            CatalogPrinter printer;
            printer.id              = id;
            printer.vendor_id       = candidate.vendor_id;
            printer.model_id        = candidate.model_id;
            printer.device_model_id = candidate.device_model_id;
            printer.build_volume    = candidate.build_volume;
            printer.picture         = candidate.artwork_path;
            printer.default_plate   = candidate.default_plate;
            name_printer(printer, candidate.vendor_name, candidate.model_name);
            printers.push_back(std::move(printer));
        }
        CatalogPrinter& printer = printers[index];
        const double    nozzle  = nozzle_of(candidate.variant);
        if (nozzle > 0. && std::find(printer.nozzles.begin(), printer.nozzles.end(), nozzle) == printer.nozzles.end()) {
            printer.nozzles.push_back(nozzle);
            printer.filaments.push_back(candidate.default_material);
        }
    }
    for (CatalogPrinter& printer : printers) {
        std::vector<std::size_t> order(printer.nozzles.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::sort(order.begin(), order.end(),
                  [&printer](std::size_t lhs, std::size_t rhs) { return printer.nozzles[lhs] < printer.nozzles[rhs]; });
        std::vector<double>      nozzles;
        std::vector<std::string> filaments;
        for (std::size_t i : order) {
            nozzles.push_back(printer.nozzles[i]);
            filaments.push_back(printer.filaments[i]);
        }
        printer.nozzles   = std::move(nozzles);
        printer.filaments = std::move(filaments);
    }
    return printers;
}

std::vector<const PrinterCandidate*> PrinterCatalog::models() const
{
    std::vector<const PrinterCandidate*> result;
    std::map<std::string, std::size_t> indexes;
    for (const PrinterCandidate& candidate : m_candidates) {
        const auto [found, inserted] = indexes.emplace(candidate.vendor_id + "\n" + candidate.model_id, result.size());
        if (inserted)
            result.push_back(&candidate);
        else if (candidate.variant == "0.4" && result[found->second]->variant != "0.4")
            result[found->second] = &candidate;
    }
    return result;
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
