#include "ManufacturingHistory.hpp"

#include <nlohmann/json.hpp>
#include <openssl/sha.h>

#include <array>
#include <iomanip>
#include <sstream>

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

std::string deterministic_output_hash(const std::string& manufacturing_hash,
                                      const std::string& slicer_version,
                                      const std::string& configuration_provenance)
{
    const json canonical{{"format", "jusprin-gcode-v1"},
                         {"manufacturingInputHash", manufacturing_hash},
                         {"slicerVersion", slicer_version},
                         {"configurationProvenance", configuration_provenance}};
    return sha256_hex(canonical.dump());
}

} // namespace Slic3r::GUI::JusPrin::Agent
