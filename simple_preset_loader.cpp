#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>

namespace fs = boost::filesystem;

// Simple function to load and display preset information
void load_and_display_preset(const std::string& file_path) {
    try {
        std::cout << "Loading preset: " << file_path << std::endl;
        std::cout << "-------------------------------------------------" << std::endl;

        // Open the file
        std::ifstream file(file_path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + file_path);
        }

        // Parse JSON
        nlohmann::json preset_json;
        file >> preset_json;

        // Get preset type based on directory structure or file name
        std::string preset_type = "Unknown";

        std::string filename = fs::path(file_path).filename().string();
        fs::path parent_dir = fs::path(file_path).parent_path();
        std::string dir_name = parent_dir.filename().string();

        if (dir_name == "machine" || dir_name == "printer") {
            preset_type = "PRINTER";
        } else if (dir_name == "filament") {
            preset_type = "FILAMENT";
        } else if (dir_name == "process" || dir_name == "print") {
            preset_type = "PRINT";
        } else {
            // Try to detect from file content
            if (preset_json.contains("bed_shape") || preset_json.contains("nozzle_diameter")) {
                preset_type = "PRINTER";
            } else if (preset_json.contains("filament_type") || preset_json.contains("filament_density")) {
                preset_type = "FILAMENT";
            } else if (preset_json.contains("layer_height") || preset_json.contains("infill_density")) {
                preset_type = "PRINT";
            }
        }

        std::cout << "Detected preset type: " << preset_type << std::endl;

        // Print basic preset information
        std::cout << std::endl;
        std::cout << "=== Preset Information ===" << std::endl;

        // Name
        std::string preset_name;
        if (preset_json.contains("name")) {
            preset_name = preset_json["name"];
        } else {
            preset_name = fs::path(file_path).stem().string();
        }
        std::cout << "Name: " << preset_name << std::endl;

        // File
        std::cout << "File: " << file_path << std::endl;

        // Inherits
        if (preset_json.contains("inherits")) {
            std::cout << "Inherits: " << preset_json["inherits"] << std::endl;
        }

        // Version
        if (preset_json.contains("version")) {
            std::cout << "Version: " << preset_json["version"] << std::endl;
        }

        // Print additional metadata
        std::cout << std::endl;
        std::cout << "=== Additional Metadata ===" << std::endl;
        std::vector<std::string> metadata_keys = {
            "from", "setting_id", "base_id", "user_id", "filament_id",
            "description", "updated_time", "type", "custom_defined"
        };

        for (const auto& key : metadata_keys) {
            if (preset_json.contains(key) && !preset_json[key].is_null()) {
                std::cout << key << ": " << preset_json[key].dump() << std::endl;
            }
        }

        // Print configuration values
        std::cout << std::endl;
        std::cout << "=== Configuration Values ===" << std::endl;

        // Get all keys except metadata keys
        std::vector<std::string> config_keys;
        for (auto it = preset_json.begin(); it != preset_json.end(); ++it) {
            bool is_metadata = false;
            for (const auto& meta_key : metadata_keys) {
                if (it.key() == meta_key || it.key() == "name" || it.key() == "inherits" || it.key() == "version") {
                    is_metadata = true;
                    break;
                }
            }
            if (!is_metadata) {
                config_keys.push_back(it.key());
            }
        }

        // Sort keys for consistent output
        std::sort(config_keys.begin(), config_keys.end());

        for (const auto& key : config_keys) {
            std::cout << key << ": " << preset_json[key].dump() << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " path_to_preset_file" << std::endl;
        return 1;
    }

    std::string file_path = argv[1];

    try {
        // Load and display the preset
        load_and_display_preset(file_path);
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}