#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <fstream>
#include <string>
#include <map>
#include <memory>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"

using namespace Slic3r;
using namespace testing;
using json = nlohmann::json;
namespace fs = std::filesystem;

// Define the static maps for SupportMaterialStyle
static t_config_enum_values s_keys_map_SupportMaterialStyle {
    { "default",        smsDefault },
    { "grid",           smsGrid },
    { "snug",           smsSnug },
    { "tree_slim",      smsTreeSlim },
    { "tree_strong",    smsTreeStrong },
    { "tree_hybrid",    smsTreeHybrid },
    { "organic",        smsOrganic }
};

// Helper function to create enum names from keys map
static t_config_enum_names enum_names_from_keys_map(const t_config_enum_values &enum_keys_map)
{
    t_config_enum_names names;
    int cnt = 0;
    for (const auto& kvp : enum_keys_map)
        cnt = std::max(cnt, kvp.second);
    cnt += 1;
    names.assign(cnt, "");
    for (const auto& kvp : enum_keys_map)
        names[kvp.second] = kvp.first;
    return names;
}

static t_config_enum_names s_keys_names_SupportMaterialStyle = enum_names_from_keys_map(s_keys_map_SupportMaterialStyle);

template<> const t_config_enum_values& ConfigOptionEnum<SupportMaterialStyle>::get_enum_values() { return s_keys_map_SupportMaterialStyle; }
template<> const t_config_enum_names& ConfigOptionEnum<SupportMaterialStyle>::get_enum_names() { return s_keys_names_SupportMaterialStyle; }

// BBL JSON key constants
#define BBL_JSON_KEY_VERSION "version"
#define BBL_JSON_KEY_NAME "name"
#define BBL_JSON_KEY_TYPE "type"
#define BBL_JSON_KEY_INHERITS "inherits"

// Mock class for ConfigDef to make add method public
class MockConfigDef : public ConfigDef {
public:
    using ConfigDef::add;
};

// Mock class for ConfigBase to test the load_from_json method
class MockConfigBase : public ConfigBase {
public:
    MockConfigDef mock_config_def;

    // Override pure virtual methods
    const ConfigDef* def() const override { return &mock_config_def; }

    const ConfigOption* optptr(const t_config_option_key &opt_key) const override {
        auto it = options.find(opt_key);
        return (it == options.end()) ? nullptr : it->second.get();
    }

    ConfigOption* optptr(const t_config_option_key &opt_key, bool create = false) override {
        if (create && options.find(opt_key) == options.end()) {
            options[opt_key] = std::unique_ptr<ConfigOption>(create_option(opt_key));
        }
        auto it = options.find(opt_key);
        return (it == options.end()) ? nullptr : it->second.get();
    }

    t_config_option_keys keys() const override {
        t_config_option_keys keys;
        for (const auto& pair : options) {
            keys.push_back(pair.first);
        }
        return keys;
    }

    // Mock implementation for handle_legacy_composite
    void handle_legacy_composite() override {
        legacy_composite_called = true;
    }

    // Mock implementation for handle_legacy
    void handle_legacy(t_config_option_key &opt_key, std::string &value) const override {
        if (opt_key == "legacy_key") {
            opt_key = "new_key";
            value = "new_value";
        }
    }

    // Helper to create options
    ConfigOption* create_option(const std::string& key) {
        if (key == "support_type") {
            return new ConfigOptionString("normal");
        } else if (key == "support_style") {
            return new ConfigOptionEnum<Slic3r::SupportMaterialStyle>(Slic3r::smsDefault);
        } else if (key == "is_infill_first") {
            return new ConfigOptionBool(false);
        } else if (key == "wall_infill_order") {
            return new ConfigOptionString("inner-outer wall/infill");
        } else if (key == "different_settings_to_system") {
            auto opt = new ConfigOptionStrings();
            // Initialize with empty strings to match the size expected in the code
            opt->values.resize(2);
            return opt;
        } else if (key == "filament_settings_id") {
            auto opt = new ConfigOptionStrings();
            opt->values.push_back("1");
            return opt;
        } else if (key == "wall_sequence") {
            return new ConfigOptionString("default");
        } else {
            return new ConfigOptionString("default");
        }
    }

    // Test access
    bool legacy_composite_called = false;
    std::map<std::string, std::unique_ptr<ConfigOption>> options;
};

class ConfigBaseLoadFromJsonTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary directory for test files
        temp_dir = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
        boost::filesystem::create_directories(temp_dir);

        // Setup the mock config def with necessary options
        setup_mock_config_def();
    }

    void TearDown() override {
        // Clean up temporary directory
        boost::filesystem::remove_all(temp_dir);
    }

    void setup_mock_config_def() {
        // Add necessary option definitions to the mock config def
        ConfigOptionDef* support_type_def = config.mock_config_def.add(std::string("support_type"), coString);
        ConfigOptionDef* support_style_def = config.mock_config_def.add(std::string("support_style"), coEnum);
        ConfigOptionDef* is_infill_first_def = config.mock_config_def.add(std::string("is_infill_first"), coBool);
        ConfigOptionDef* wall_infill_order_def = config.mock_config_def.add(std::string("wall_infill_order"), coString);
        ConfigOptionDef* different_settings_def = config.mock_config_def.add(std::string("different_settings_to_system"), coStrings);
        ConfigOptionDef* filament_settings_id_def = config.mock_config_def.add(std::string("filament_settings_id"), coStrings);
        ConfigOptionDef* wall_sequence_def = config.mock_config_def.add(std::string("wall_sequence"), coString);
        ConfigOptionDef* test_key_def = config.mock_config_def.add(std::string("test_key"), coString);
        ConfigOptionDef* array_key_def = config.mock_config_def.add(std::string("array_key"), coString);
        ConfigOptionDef* new_key_def = config.mock_config_def.add(std::string("new_key"), coString);
        ConfigOptionDef* legacy_key_def = config.mock_config_def.add(std::string("legacy_key"), coString);
    }

    // Helper to create a test JSON file
    std::string create_test_json(const json& j) const {
        std::string file_path = (temp_dir / "test.json").string();
        boost::nowide::ofstream file(file_path);
        file << j.dump(4);
        file.close();
        return file_path;
    }

    boost::filesystem::path temp_dir;
    MockConfigBase config;
};

// Test successful loading of a simple JSON file
TEST_F(ConfigBaseLoadFromJsonTest, BasicJsonLoading) {
    json j = {
        {BBL_JSON_KEY_VERSION, "1.0.0"},
        {BBL_JSON_KEY_NAME, "test_config"},
        {BBL_JSON_KEY_TYPE, "test_type"},
        {"test_key", "test_value"}
    };

    std::string file_path = create_test_json(j);
    std::map<std::string, std::string> key_values;
    std::string reason;

    ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Enable);
    int result = config.load_from_json(file_path, ctx, true, key_values, reason);

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(reason.empty());
    EXPECT_TRUE(config.legacy_composite_called);

    EXPECT_EQ(key_values[BBL_JSON_KEY_VERSION], "1.0.0");
    EXPECT_EQ(key_values[BBL_JSON_KEY_NAME], "test_config");
    EXPECT_EQ(key_values[BBL_JSON_KEY_TYPE], "test_type");

    // Check that the option was set
    ConfigOption* opt = config.option("test_key");
    ASSERT_NE(opt, nullptr);
    EXPECT_EQ(opt->serialize(), "test_value");
}

// Test loading with array values
TEST_F(ConfigBaseLoadFromJsonTest, ArrayValues) {
    json j = {
        {BBL_JSON_KEY_VERSION, "1.0.0"},
        {"array_key", json::array({"value1", "value2", "value3"})}
    };

    std::string file_path = create_test_json(j);
    std::map<std::string, std::string> key_values;
    std::string reason;

    ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Enable);
    int result = config.load_from_json(file_path, ctx, true, key_values, reason);

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(reason.empty());

    // Check that the array option was set correctly
    ConfigOption* opt = config.option("array_key");
    ASSERT_NE(opt, nullptr);
    // The serialized value might be different depending on the implementation
    // Just check that it contains all the values
    std::string serialized = opt->serialize();
    EXPECT_TRUE(serialized.find("value1") != std::string::npos);
    EXPECT_TRUE(serialized.find("value2") != std::string::npos);
    EXPECT_TRUE(serialized.find("value3") != std::string::npos);
}

// Test handling of legacy keys
TEST_F(ConfigBaseLoadFromJsonTest, LegacyKeyHandling) {
    json j = {
        {BBL_JSON_KEY_VERSION, "1.0.0"},
        {"legacy_key", "old_value"}
    };

    std::string file_path = create_test_json(j);
    std::map<std::string, std::string> key_values;
    std::string reason;

    ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Enable);
    int result = config.load_from_json(file_path, ctx, true, key_values, reason);

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(reason.empty());

    // Check that the legacy key was transformed
    ConfigOption* opt = config.option("new_key");
    ASSERT_NE(opt, nullptr);
    EXPECT_EQ(opt->serialize(), "new_value");
}

// Test project settings handling
TEST_F(ConfigBaseLoadFromJsonTest, ProjectSettingsHandling) {
    json j = {
        {BBL_JSON_KEY_VERSION, "1.0.0"},
        {BBL_JSON_KEY_NAME, "project_settings"},
        {"support_type", "hybrid(auto)"},
        {"wall_infill_order", "infill/outer wall/inner wall"}
    };

    std::string file_path = create_test_json(j);
    std::map<std::string, std::string> key_values;
    std::string reason;

    ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Enable);
    int result = config.load_from_json(file_path, ctx, true, key_values, reason);

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(reason.empty());

    // Check that support_style was set correctly
    ConfigOption* support_style = config.option("support_style");
    ASSERT_NE(support_style, nullptr);
    EXPECT_EQ(support_style->getInt(), (int)Slic3r::smsTreeHybrid);

    // Check that is_infill_first was set correctly
    ConfigOption* is_infill_first = config.option("is_infill_first");
    ASSERT_NE(is_infill_first, nullptr);
    EXPECT_TRUE(is_infill_first->getBool());

    // Check that different_settings_to_system was updated
    ConfigOptionStrings* diff_settings = dynamic_cast<ConfigOptionStrings*>(config.option("different_settings_to_system"));
    ASSERT_NE(diff_settings, nullptr);
    EXPECT_GT(diff_settings->values.size(), 0);
}

// Test handling of file read errors
TEST_F(ConfigBaseLoadFromJsonTest, FileReadError) {
    std::string non_existent_file = (temp_dir / "non_existent.json").string();
    std::map<std::string, std::string> key_values;
    std::string reason;

    ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Enable);
    int result = config.load_from_json(non_existent_file, ctx, true, key_values, reason);

    EXPECT_EQ(result, -1);
    EXPECT_FALSE(reason.empty());
    EXPECT_TRUE(reason.find("parse_error") != std::string::npos);
}

// Test handling of invalid JSON
TEST_F(ConfigBaseLoadFromJsonTest, InvalidJson) {
    // Create a file with invalid JSON
    std::string file_path = (temp_dir / "invalid.json").string();
    {
        boost::nowide::ofstream file(file_path);
        file << "{ invalid json";
        file.close();
    }

    std::map<std::string, std::string> key_values;
    std::string reason;

    ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Enable);
    int result = config.load_from_json(file_path, ctx, true, key_values, reason);

    EXPECT_EQ(result, -1);
    EXPECT_FALSE(reason.empty());
    EXPECT_TRUE(reason.find("JsonParseError") != std::string::npos);
}

// Test handling of missing config definition
TEST_F(ConfigBaseLoadFromJsonTest, MissingConfigDef) {
    json j = {
        {BBL_JSON_KEY_VERSION, "1.0.0"},
        {"test_key", "test_value"}
    };

    std::string file_path = create_test_json(j);
    std::map<std::string, std::string> key_values;
    std::string reason;

    // Create a mock with null config def
    class NullDefMockConfigBase : public MockConfigBase {
        const ConfigDef* def() const override { return nullptr; }
    };

    NullDefMockConfigBase null_def_config;
    ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Enable);
    int result = null_def_config.load_from_json(file_path, ctx, true, key_values, reason);

    EXPECT_EQ(result, -1);
}

// Test handling of inherits key
TEST_F(ConfigBaseLoadFromJsonTest, InheritsHandling) {
    std::string file_path = "temp_config.json";
    std::ofstream out(file_path);
    out << R"({
        "inherits": "test_key",
        "version": "1.0.0"
    })";
    out.close();

    ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Disable);
    std::map<std::string, std::string> key_values;
    std::string reason;

    // When load_inherits_to_config is false, it should:
    // 1. Return success (0)
    // 2. Have empty reason string
    // 3. Store the inherits key in key_values
    int result = config.load_from_json(file_path, ctx, false, key_values, reason);
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(reason.empty());
    EXPECT_EQ(key_values["inherits"], "test_key");

    // When load_inherits_to_config is true, it should:
    // 1. Return error (-1)
    // 2. Set reason string with error
    // 3. Not store the inherits key
    key_values.clear();
    reason.clear();
    result = config.load_from_json(file_path, ctx, true, key_values, reason);
    EXPECT_EQ(result, -1);
    EXPECT_FALSE(reason.empty());
    EXPECT_EQ(key_values.find("inherits"), key_values.end());

    std::filesystem::remove(file_path);
}

// Test handling of generic exception
TEST_F(ConfigBaseLoadFromJsonTest, GenericException) {
    // Create a mock that throws a generic exception
    class ThrowingMockConfigBase : public MockConfigBase {
    public:
        ConfigOption* optptr(const t_config_option_key &opt_key, bool create = false) override {
            throw std::runtime_error("Test exception");
        }
    };

    json j = {
        {BBL_JSON_KEY_VERSION, "1.0.0"},
        {"test_key", "test_value"}
    };

    std::string file_path = create_test_json(j);
    std::map<std::string, std::string> key_values;
    std::string reason;

    ThrowingMockConfigBase throwing_config;
    ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Enable);
    int result = throwing_config.load_from_json(file_path, ctx, true, key_values, reason);

    EXPECT_EQ(result, -1);
    EXPECT_FALSE(reason.empty());
    EXPECT_TRUE(reason.find("std::exception") != std::string::npos);
}

// Test handling of special wall_infill_order values
TEST_F(ConfigBaseLoadFromJsonTest, WallInfillOrderHandling) {
    // Test each special case for wall_infill_order
    std::vector<std::string> test_values = {
        "outer wall/inner wall/infill",
        "infill/outer wall/inner wall",
        "inner-outer-inner wall/infill"
    };

    for (const auto& test_value : test_values) {
        json j = {
            {BBL_JSON_KEY_VERSION, "1.0.0"},
            {BBL_JSON_KEY_NAME, "project_settings"},
            {"wall_infill_order", test_value}
        };

        std::string file_path = create_test_json(j);
        std::map<std::string, std::string> key_values;
        std::string reason;

        // Reset config for each test
        config = MockConfigBase();
        setup_mock_config_def();

        ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Enable);
        int result = config.load_from_json(file_path, ctx, true, key_values, reason);

        EXPECT_EQ(result, 0);
        EXPECT_TRUE(reason.empty());

        // Check if is_infill_first is set for specific values
        if (test_value == "infill/outer wall/inner wall" || test_value == "infill/inner wall/outer wall") {
            ConfigOption* is_infill_first = config.option("is_infill_first");
            ASSERT_NE(is_infill_first, nullptr);
            EXPECT_TRUE(is_infill_first->getBool());
        }
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}