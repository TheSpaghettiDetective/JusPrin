#include "PrinterCatalog.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
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

        for (const auto& entry : index["machine_model_list"]) {
            const std::string sub_path = entry.value("sub_path", "");
            if (sub_path.empty()) continue;
            const json model = read_json(vendor_dir / sub_path);
            const std::string model_name = model.value("name", entry.value("name", ""));
            const std::string model_id = model_name;
            const std::string device_model = model.value("model_id", "");
            const auto variants = split(first_string(model.value("nozzle_diameter", json())), ';');
            const auto materials = split(first_string(model.value("default_materials", json())), ';');
            const std::string plate = first_string(model.value("default_bed_type", json()));
            fs::path artwork = vendor_dir / (model_name + "_cover.png");
            if (!fs::exists(artwork)) artwork.clear();

            for (const std::string& variant : variants) {
                const std::string preset_name = model_name + " " + variant + " nozzle";
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
                candidate.default_material = !materials.empty() ? materials.front()
                    : first_string(machine.value("default_filament_profile", json()));
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
