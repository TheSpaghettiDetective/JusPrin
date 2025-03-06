#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <fstream>
#include <boost/nowide/fstream.hpp>
#include "nlohmann/json.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PrintConfig.hpp"

using json = nlohmann::json;

// Simplified version of printing all config options
void print_config_options(const Slic3r::DynamicPrintConfig& config) {
    // Print all configuration options
    for (const std::string& key : config.keys()) {
        const Slic3r::ConfigOption* opt = config.option(key);
        if (opt) {
            std::cout << key << ": " << opt->serialize() << std::endl;
        }
    }
}

// Simplified JSON file loading function
bool load_json_config(const std::string& file_path, Slic3r::DynamicPrintConfig& config) {
    try {
        boost::nowide::ifstream ifs(file_path);
        if (!ifs.is_open()) {
            std::cerr << "Error: Could not open file " << file_path << std::endl;
            return false;
        }

        // Parse JSON
        json j;
        ifs >> j;
        ifs.close();

        // Load configuration
        Slic3r::ConfigSubstitutionContext ctx(Slic3r::ForwardCompatibilitySubstitutionRule::Disable);

        // Extract each key-value pair from the JSON and set it in the config
        for (auto it = j.begin(); it != j.end(); ++it) {
            const std::string& key = it.key();
            
            // Skip non-configuration entries
            if (key == "version" || key == "name" || key == "inherits" || 
                key == "from" || key == "setting_id" || key == "base_id" || 
                key == "user_id" || key == "filament_id") {
                continue;
            }
            
            // Convert JSON value to string
            std::string value;
            if (it.value().is_string()) {
                value = it.value().get<std::string>();
            } else {
                value = it.value().dump();
            }
            
            // Try to set the config value
            try {
                config.set_deserialize(key, value, ctx);
            } catch (const std::exception& e) {
                std::cerr << "Warning: Failed to set config value for " << key << ": " << e.what() << std::endl;
            }
        }
        
        return true;
    } catch (const json::parse_error& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return false;
    }
}

// Direct JSON printing function - simpler approach
void print_json_key_values(const std::string& file_path) {
    try {
        boost::nowide::ifstream ifs(file_path);
        if (!ifs.is_open()) {
            std::cerr << "Error: Could not open file " << file_path << std::endl;
            return;
        }

        // Parse JSON
        json j;
        ifs >> j;
        ifs.close();

        // Print all key-value pairs directly from JSON
        for (auto it = j.begin(); it != j.end(); ++it) {
            std::cout << it.key() << ": ";
            
            if (it.value().is_string()) {
                std::cout << it.value().get<std::string>();
            } else if (it.value().is_array() || it.value().is_object()) {
                std::cout << it.value().dump();
            } else {
                std::cout << it.value();
            }
            
            std::cout << std::endl;
        }
    } catch (const json::parse_error& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
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
    std::cout << "Loading preset file: " << file_path << std::endl;

    // Method 1: Simply print all JSON key-value pairs (simpler and more reliable)
    std::cout << "\n--- Direct JSON Key-Value Pairs ---\n" << std::endl;
    print_json_key_values(file_path);

    // Method 2: Load into DynamicPrintConfig and print (may miss some values)
    std::cout << "\n--- Loaded Config Options ---\n" << std::endl;
    Slic3r::DynamicPrintConfig config;
    if (load_json_config(file_path, config)) {
        print_config_options(config);
    }

    return 0;
}