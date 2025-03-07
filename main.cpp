#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <fstream>
#include <boost/nowide/fstream.hpp>
#include <boost/filesystem.hpp>

// JusPrin/OrcaSlicer includes
#include "libslic3r/Config.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/AppConfig.hpp"

namespace fs = boost::filesystem;

// Load preset using PresetBundle
void load_and_print_preset(const std::string& file_path) {
    try {
        std::cout << "Loading preset: " << file_path << std::endl;
        std::cout << "-------------------------------------------------" << std::endl;
        
        // Create a PresetBundle
        Slic3r::PresetBundle preset_bundle;
        
        // Get preset type based on directory structure or file name
        Slic3r::Preset::Type preset_type = Slic3r::Preset::TYPE_INVALID;
        
        std::string filename = fs::path(file_path).filename().string();
        fs::path parent_dir = fs::path(file_path).parent_path();
        std::string dir_name = parent_dir.filename().string();
        
        if (dir_name == "machine" || dir_name == "printer") {
            preset_type = Slic3r::Preset::TYPE_PRINTER;
            std::cout << "Detected preset type: PRINTER" << std::endl;
        } else if (dir_name == "filament") {
            preset_type = Slic3r::Preset::TYPE_FILAMENT;
            std::cout << "Detected preset type: FILAMENT" << std::endl;
        } else if (dir_name == "process" || dir_name == "print") {
            preset_type = Slic3r::Preset::TYPE_PRINT;
            std::cout << "Detected preset type: PRINT" << std::endl;
        } else {
            // Try to detect from file name or content
            std::cout << "Could not detect preset type from directory structure, analyzing file..." << std::endl;
            
            // Try loading as config bundle first
            if (preset_bundle.load_config_file(file_path, false, true)) {
                std::cout << "Successfully loaded as a config bundle" << std::endl;
                return;
            }
            
            // If not a bundle, try to infer type from file contents
            boost::nowide::ifstream ifs(file_path);
            if (!ifs.is_open()) {
                throw std::runtime_error("Failed to open file: " + file_path);
            }
            
            nlohmann::json j;
            ifs >> j;
            
            // Check for specific configuration options typical for each preset type
            if (j.contains("bed_shape") || j.contains("nozzle_diameter")) {
                preset_type = Slic3r::Preset::TYPE_PRINTER;
                std::cout << "Detected preset type from content: PRINTER" << std::endl;
            } else if (j.contains("filament_type") || j.contains("filament_density")) {
                preset_type = Slic3r::Preset::TYPE_FILAMENT;
                std::cout << "Detected preset type from content: FILAMENT" << std::endl;
            } else if (j.contains("layer_height") || j.contains("infill_density")) {
                preset_type = Slic3r::Preset::TYPE_PRINT;
                std::cout << "Detected preset type from content: PRINT" << std::endl;
            } else {
                throw std::runtime_error("Could not determine preset type from file content");
            }
        }
        
        // Load configuration
        Slic3r::DynamicPrintConfig config;
        Slic3r::ForwardCompatibilitySubstitutionRule rule = Slic3r::ForwardCompatibilitySubstitutionRule::EnableSilent;
        std::map<std::string, std::string> key_values;
        std::string reason;
        
        // Load the configuration from the JSON file
        Slic3r::ConfigSubstitutions substitutions = config.load_from_json(file_path, rule, key_values, reason);
        
        if (!substitutions.empty()) {
            std::cout << "Note: Some configuration values were substituted during loading." << std::endl;
            for (const auto& subst : substitutions) {
                std::cout << "  - " << subst.opt_def->opt_key << ": " 
                          << subst.old_value << " -> " 
                          << subst.new_value->serialize() << std::endl;
            }
            std::cout << std::endl;
        }
        
        // Get preset name
        std::string preset_name;
        if (key_values.find("name") != key_values.end()) {
            preset_name = key_values["name"];
        } else {
            preset_name = fs::path(file_path).stem().string();
        }
        
        // Get preset collection based on type
        Slic3r::PresetCollection* collection = nullptr;
        switch (preset_type) {
            case Slic3r::Preset::TYPE_PRINT:
                collection = &preset_bundle.prints;
                break;
            case Slic3r::Preset::TYPE_FILAMENT:
                collection = &preset_bundle.filaments;
                break;
            case Slic3r::Preset::TYPE_PRINTER:
                collection = &preset_bundle.printers;
                break;
            default:
                throw std::runtime_error("Unsupported preset type");
        }
        
        // Load the preset into the collection
        // Get version
        Slic3r::Semver version;
        if (key_values.find("version") != key_values.end()) {
            version = Slic3r::Semver::parse(key_values["version"]).value_or(Slic3r::Semver());
        }
        
        // Is custom?
        bool is_custom = false;
        if (key_values.find("custom_defined") != key_values.end()) {
            is_custom = (key_values["custom_defined"] == "1");
        }
        
        // Call the load_preset function
        Slic3r::Preset& preset = collection->load_preset(file_path, preset_name, std::move(config), true, version, is_custom);
        
        // Print basic preset information
        std::cout << std::endl;
        std::cout << "=== Preset Information ===" << std::endl;
        std::cout << "Name: " << preset.name << std::endl;
        std::cout << "File: " << preset.file << std::endl;
        if (!preset.inherits().empty()) {
            std::cout << "Inherits: " << preset.inherits() << std::endl;
        }
        if (preset.version.valid()) {
            std::cout << "Version: " << preset.version.to_string() << std::endl;
        }
        
        // Print additional metadata
        std::cout << std::endl;
        std::cout << "=== Additional Metadata ===" << std::endl;
        std::vector<std::string> metadata_keys = {
            "from", "setting_id", "base_id", "user_id", "filament_id", 
            "description", "updated_time", "type", "custom_defined"
        };
        
        for (const auto& key : metadata_keys) {
            auto it = key_values.find(key);
            if (it != key_values.end() && !it->second.empty()) {
                std::cout << key << ": " << it->second << std::endl;
            }
        }
        
        // Print configuration values
        std::cout << std::endl;
        std::cout << "=== Configuration Values ===" << std::endl;
        
        // Get sorted keys for consistent output
        std::vector<std::string> config_keys = preset.config.keys();
        std::sort(config_keys.begin(), config_keys.end());
        
        for (const auto& key : config_keys) {
            const Slic3r::ConfigOption* opt = preset.config.option(key);
            if (opt) {
                std::cout << key << ": " << opt->serialize() << std::endl;
            }
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
        // Initialize Slic3r AppConfig
        Slic3r::AppConfig app_config;
        
        // Load and print the preset
        load_and_print_preset(file_path);
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}