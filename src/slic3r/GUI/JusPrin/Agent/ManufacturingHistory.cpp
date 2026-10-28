#include "ManufacturingHistory.hpp"

#include <nlohmann/json.hpp>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace Slic3r::GUI::JusPrin::Agent {

using nlohmann::json;

std::string sha256_hex(const std::string& bytes)
{
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), digest.data());
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned char byte : digest)
        out << std::setw(2) << static_cast<unsigned int>(byte);
    return out.str();
}

std::string sha256_file_hex(const std::string& path)
{
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + path);
    SHA256_CTX context;
    SHA256_Init(&context);
    std::array<char, 1 << 16> buffer{};
    while (in.read(buffer.data(), buffer.size()) || in.gcount() > 0)
        SHA256_Update(&context, buffer.data(), static_cast<std::size_t>(in.gcount()));
    if (in.bad())
        throw std::runtime_error("cannot read " + path);
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256_Final(digest.data(), &context);
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned char byte : digest)
        out << std::setw(2) << static_cast<unsigned int>(byte);
    return out.str();
}

namespace {

std::filesystem::path kept_gcode_dir(const std::string& project_dir)
{
    return std::filesystem::u8path(project_dir) / "gcode";
}

} // namespace

std::string keep_gcode_copy(const std::string& project_dir, const std::string& hash, const std::string& source,
                            std::size_t keep)
{
    namespace fs = std::filesystem;
    if (hash.size() != 64 || hash.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::logic_error("a kept G-code is named by its SHA-256, not " + hash);
    const fs::path dir = kept_gcode_dir(project_dir);
    fs::create_directories(dir);
    const fs::path copy = dir / (hash + ".gcode");
    if (fs::exists(copy)) {
        // The same bytes left again: it is the newest copy now.
        fs::last_write_time(copy, fs::file_time_type::clock::now());
    } else {
        const fs::path pending = dir / (hash + ".pending");
        fs::copy_file(fs::u8path(source), pending, fs::copy_options::overwrite_existing);
        fs::rename(pending, copy);
    }

    std::vector<std::pair<fs::file_time_type, fs::path>> copies;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir))
        if (entry.is_regular_file() && entry.path().extension() == ".gcode")
            copies.emplace_back(entry.last_write_time(), entry.path());
    if (copies.size() > keep) {
        std::sort(copies.begin(), copies.end(), [](const auto& left, const auto& right) { return left.first > right.first; });
        for (std::size_t index = keep; index < copies.size(); ++index)
            if (copies[index].second != copy) {
                // A G-code's package goes with it.
                const std::string pruned = copies[index].second.stem().u8string();
                fs::remove(copies[index].second);
                fs::remove(dir / (pruned + ".gcode.3mf"));
                fs::remove(dir / (pruned + ".config.3mf"));
            }
    }
    return copy.u8string();
}

std::string kept_gcode_copy(const std::string& project_dir, const std::string& hash)
{
    const std::filesystem::path copy = kept_gcode_dir(project_dir) / (hash + ".gcode");
    return std::filesystem::exists(copy) ? copy.u8string() : std::string();
}

std::string keep_gcode_package(const std::string& project_dir, const std::string& hash, const std::string& package,
                               const std::string& config)
{
    namespace fs = std::filesystem;
    if (kept_gcode_copy(project_dir, hash).empty())
        throw std::logic_error("a package is kept beside its G-code's copy, and G-code " + hash + " has none");
    const fs::path dir = kept_gcode_dir(project_dir);
    const auto keep = [&dir](const std::string& source, const fs::path& target) {
        const fs::path pending = dir / (target.filename().u8string() + ".pending");
        fs::copy_file(fs::u8path(source), pending, fs::copy_options::overwrite_existing);
        fs::rename(pending, target);
    };
    keep(package, dir / (hash + ".gcode.3mf"));
    // A config left from an earlier cloud send belongs to that send's package.
    if (config.empty())
        fs::remove(dir / (hash + ".config.3mf"));
    else
        keep(config, dir / (hash + ".config.3mf"));
    return sha256_file_hex((dir / (hash + ".gcode.3mf")).u8string());
}

std::optional<KeptPackage> kept_gcode_package(const std::string& project_dir, const std::string& hash)
{
    namespace fs = std::filesystem;
    const fs::path dir     = kept_gcode_dir(project_dir);
    const fs::path package = dir / (hash + ".gcode.3mf");
    if (kept_gcode_copy(project_dir, hash).empty() || !fs::exists(package))
        return std::nullopt;
    const fs::path config = dir / (hash + ".config.3mf");
    return KeptPackage{package.u8string(), fs::exists(config) ? config.u8string() : std::string()};
}

std::string kept_package_hash(const GcodeRecord& record)
{
    for (auto destination = record.destinations.rbegin(); destination != record.destinations.rend(); ++destination)
        if (!destination->package_hash.empty())
            return destination->package_hash;
    return {};
}

std::string gcode_display_name(const GcodeRecord& record)
{
    for (auto destination = record.destinations.rbegin(); destination != record.destinations.rend(); ++destination)
        if (destination->kind == GcodeDestination::Kind::Export && !destination->path.empty())
            return std::filesystem::u8path(destination->path).filename().u8string();
    return record.plate_name;
}

std::optional<std::string> manufacturing_input_hash(const Workspace::WorkspaceSnapshot& snapshot,
                                                    std::size_t                         plate_index)
{
    if (plate_index >= snapshot.plates.size())
        return std::nullopt;

    const Workspace::WorkspacePlate& plate = snapshot.plates[plate_index];
    json objects = json::array();
    for (const Workspace::WorkspaceObject& object : plate.objects) {
        json instances = json::array();
        for (const Workspace::ObjectTransform& transform : object.instances)
            instances.push_back(json{{"position", transform.position},
                                     {"rotation", transform.rotation},
                                     {"scale", transform.scale}});
        // Object display names and session-scoped IDs are intentionally not
        // manufacturing inputs. Geometry changes exposed by this workspace
        // surface appear as object/instance topology or transforms.
        objects.push_back(json{{"instances", std::move(instances)}});
    }
    const json canonical{{"format", "jusprin-manufacturing-input-v1"},
                         {"plateIndex", plate_index},
                         {"printerPreset", snapshot.setup.printer_preset},
                         {"materialPreset", snapshot.setup.filament_preset},
                         {"objects", std::move(objects)}};
    return sha256_hex(canonical.dump());
}

std::optional<GcodeRecord> gcode_record(const Workspace::WorkspaceSnapshot& snapshot, std::size_t plate_index)
{
    const std::optional<std::string> hash = manufacturing_input_hash(snapshot, plate_index);
    if (!hash)
        return std::nullopt;
    const Workspace::WorkspacePlate& plate = snapshot.plates[plate_index];
    GcodeRecord record;
    record.plate_index              = plate_index;
    record.plate_name               = plate.name;
    record.printer                  = snapshot.setup.printer_preset;
    record.material                 = snapshot.setup.filament_preset;
    record.process                  = snapshot.setup.process_preset;
    record.manufacturing_input_hash = *hash;
    // An estimate OrcaSlicer did not give stays zero, which the record reads
    // as not recorded.
    if (plate.estimate) {
        if (plate.estimate->has_time)
            record.statistics.print_time_seconds = plate.estimate->print_time_seconds;
        if (plate.estimate->has_material)
            record.statistics.material_grams = plate.estimate->material_grams;
        if (plate.estimate->has_cost)
            record.statistics.material_cost = plate.estimate->material_cost;
    }
    return record;
}

} // namespace Slic3r::GUI::JusPrin::Agent
