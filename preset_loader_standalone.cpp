#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Simple function to pretty-print JSON values
void print_json_value(const json& value, int indent = 0) {
    std::string indentation(indent, ' ');
    
    if (value.is_object()) {
        std::cout << "{" << std::endl;
        for (auto it = value.begin(); it != value.end(); ++it) {
            std::cout << indentation + "  " << it.key() << ": ";
            print_json_value(it.value(), indent + 2);
            if (std::next(it) != value.end()) {
                std::cout << ",";
            }
            std::cout << std::endl;
        }
        std::cout << indentation << "}";
    } else if (value.is_array()) {
        std::cout << "[" << std::endl;
        for (auto it = value.begin(); it != value.end(); ++it) {
            std::cout << indentation + "  ";
            print_json_value(*it, indent + 2);
            if (std::next(it) != value.end()) {
                std::cout << ",";
            }
            std::cout << std::endl;
        }
        std::cout << indentation << "]";
    } else if (value.is_string()) {
        std::cout << "\"" << value.get<std::string>() << "\"";
    } else if (value.is_number() || value.is_boolean() || value.is_null()) {
        std::cout << value.dump();
    }
}

// Print all key-value pairs from a preset file
void print_preset_file(const std::string& file_path) {
    try {
        // Open the file
        std::ifstream file(file_path);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open file " << file_path << std::endl;
            return;
        }
        
        // Parse JSON
        json preset;
        file >> preset;
        file.close();
        
        // Print the main preset metadata
        std::vector<std::string> metadata_keys = {"name", "version", "inherits", "from", "setting_id", "base_id", "user_id", "filament_id"};
        
        std::cout << "=== Preset Metadata ===" << std::endl;
        for (const auto& key : metadata_keys) {
            if (preset.contains(key)) {
                std::cout << key << ": ";
                if (preset[key].is_string()) {
                    std::cout << preset[key].get<std::string>();
                } else {
                    std::cout << preset[key].dump();
                }
                std::cout << std::endl;
            }
        }
        
        // Print all configuration values
        std::cout << std::endl << "=== Configuration Values ===" << std::endl;
        
        // Sort keys for consistent output
        std::vector<std::string> keys;
        for (auto it = preset.begin(); it != preset.end(); ++it) {
            if (std::find(metadata_keys.begin(), metadata_keys.end(), it.key()) == metadata_keys.end()) {
                keys.push_back(it.key());
            }
        }
        std::sort(keys.begin(), keys.end());
        
        // Print each configuration value
        for (const auto& key : keys) {
            std::cout << key << ": ";
            if (preset[key].is_string()) {
                std::cout << preset[key].get<std::string>();
            } else if (preset[key].is_object() || preset[key].is_array()) {
                print_json_value(preset[key]);
            } else {
                std::cout << preset[key].dump();
            }
            std::cout << std::endl;
        }
        
    } catch (const json::parse_error& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " path_to_preset_file.json" << std::endl;
        return 1;
    }

    std::string file_path = argv[1];
    std::cout << "Loading preset file: " << file_path << std::endl << std::endl;
    
    print_preset_file(file_path);
    
    return 0;
}