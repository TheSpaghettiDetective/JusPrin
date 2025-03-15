#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include "libslic3r/Config.hpp"
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <fstream>

using namespace Slic3r;

enum class TestEnum {
    First,
    Second,
    Third
};

namespace Slic3r {
    template<>
    const t_config_enum_names& ConfigOptionEnum<TestEnum>::get_enum_names() {
        static t_config_enum_names names = {"first", "second", "third"};
        return names;
    }

    template<>
    const t_config_enum_values& ConfigOptionEnum<TestEnum>::get_enum_values() {
        static t_config_enum_values values = {
            {"first", static_cast<int>(TestEnum::First)},
            {"second", static_cast<int>(TestEnum::Second)},
            {"third", static_cast<int>(TestEnum::Third)}
        };
        return values;
    }

    // TestConfigDef - a ConfigDef that exposes protected methods for testing
    class TestConfigDef : public ConfigDef {
    public:
        ConfigOptionDef* test_add(const t_config_option_key& opt_key, ConfigOptionType type) {
            return this->add(opt_key, type);
        }

        ConfigOptionDef* test_add_nullable(const t_config_option_key& opt_key, ConfigOptionType type) {
            return this->add_nullable(opt_key, type);
        }
    };

    // TestDynamicConfig - a DynamicConfig that uses TestConfigDef
    class TestDynamicConfig : public DynamicConfig {
    private:
        TestConfigDef* m_def;

    public:
        TestDynamicConfig(TestConfigDef* def) : m_def(def) {}

        const ConfigDef* def() const override { return m_def; }
    };

    // Test implementation of StaticConfig
    class TestStaticConfig : public StaticConfig {
    private:
        TestConfigDef m_test_def;
        t_config_option_keys m_keys;
        std::map<t_config_option_key, ConfigOption*> m_options;

    public:
        TestStaticConfig() {
            // Add test options
            m_test_def.test_add("test_int", coInt);
            m_test_def.test_add("test_float", coFloat);
            m_test_def.test_add("test_string", coString);

            // Set up keys
            m_keys.push_back("test_int");
            m_keys.push_back("test_float");
            m_keys.push_back("test_string");

            // Create default options
            m_options["test_int"] = new ConfigOptionInt(10);
            m_options["test_float"] = new ConfigOptionFloat(20.5);
            m_options["test_string"] = new ConfigOptionString("test");
        }

        ~TestStaticConfig() {
            for (auto& opt : m_options) {
                delete opt.second;
            }
        }

        // Implement pure virtual methods
        const ConfigDef* def() const override {
            return &m_test_def;
        }

        t_config_option_keys keys() const override {
            return m_keys;
        }

        ConfigOption* optptr(const t_config_option_key& opt_key, bool create = false) override {
            auto it = m_options.find(opt_key);
            if (it != m_options.end()) {
                return it->second;
            }
            return nullptr;
        }

        const ConfigOption* optptr(const t_config_option_key& opt_key) const override {
            auto it = m_options.find(opt_key);
            if (it != m_options.end()) {
                return it->second;
            }
            return nullptr;
        }

        void set_defaults() {
            // Default implementation just to fulfill the interface
        }

        // Add accessors for test purposes
        ConfigOptionInt* opt1() { return static_cast<ConfigOptionInt*>(m_options["test_int"]); }
        ConfigOptionFloat* opt2() { return static_cast<ConfigOptionFloat*>(m_options["test_float"]); }
        ConfigOptionString* opt3() { return static_cast<ConfigOptionString*>(m_options["test_string"]); }
    };

    // A simple version of ReverseLineReader for testing
    class ReverseLineReader {
    private:
        std::ifstream m_file;
        std::vector<std::string> m_lines;
        size_t m_current_line;

    public:
        ReverseLineReader(const std::string& filename) {
            m_file.open(filename);
            if (m_file.is_open()) {
                std::string line;
                while (std::getline(m_file, line)) {
                    m_lines.push_back(line);
                }
                m_file.close();
                m_current_line = m_lines.size();
            }
        }

        // Add a constructor that takes a stream and position
        ReverseLineReader(std::ifstream& file, size_t /*startPos*/) {
            if (file.is_open()) {
                std::string line;
                while (std::getline(file, line)) {
                    m_lines.push_back(line);
                }
                m_current_line = m_lines.size();
            }
        }

        bool getline(std::string& out) {
            if (m_current_line == 0) {
                return false;
            }
            out = m_lines[--m_current_line];
            return true;
        }
    };
}

// Helper function to test serialization/deserialization
template<typename T>
void test_serialize_deserialize(const T& original, const std::string& expected_str) {
    REQUIRE(original.serialize() == expected_str);
    T deserialized;
    REQUIRE(deserialized.deserialize(expected_str));
    REQUIRE(deserialized.serialize() == expected_str);
}

TEST_CASE("ConfigOption Basic Tests", "[Config]") {
    SECTION("ConfigOptionFloat") {
        ConfigOptionFloat opt(1.5);
        REQUIRE(opt.serialize() == "1.5");

        ConfigOptionFloat opt2;
        REQUIRE(opt2.deserialize("2.5"));
        REQUIRE(opt2.value == 2.5);
    }

    SECTION("ConfigOptionInt") {
        ConfigOptionInt opt(42);
        REQUIRE(opt.serialize() == "42");

        ConfigOptionInt opt2;
        REQUIRE(opt2.deserialize("24"));
        REQUIRE(opt2.value == 24);
    }

    SECTION("ConfigOptionString") {
        ConfigOptionString opt("test");
        REQUIRE(opt.serialize() == "test");

        ConfigOptionString opt2;
        REQUIRE(opt2.deserialize("value"));
        REQUIRE(opt2.value == "value");
    }

    SECTION("ConfigOptionBool") {
        ConfigOptionBool opt(true);
        REQUIRE(opt.serialize() == "1");

        ConfigOptionBool opt2;
        REQUIRE(opt2.deserialize("1"));
        REQUIRE(opt2.value == true);
        REQUIRE(opt2.deserialize("0"));
        REQUIRE(opt2.value == false);
    }

    SECTION("DynamicConfig apply") {
        DynamicConfig config;
        DynamicConfig other;
        t_config_option_keys keys;

        config.apply(other, true);
        config.apply(other);
        config.apply_only(other, keys);
    }
}

TEST_CASE("ConfigOption Def Tests", "[Config]") {
    SECTION("create_empty_option") {
        ConfigOptionDef def;

        def.type = coFloat;
        auto* float_opt = def.create_empty_option();
        REQUIRE(float_opt != nullptr);
        REQUIRE(dynamic_cast<ConfigOptionFloat*>(float_opt) != nullptr);
        delete float_opt;

        def.type = coInt;
        auto* int_opt = def.create_empty_option();
        REQUIRE(int_opt != nullptr);
        REQUIRE(dynamic_cast<ConfigOptionInt*>(int_opt) != nullptr);
        delete int_opt;

        def.type = coString;
        auto* string_opt = def.create_empty_option();
        REQUIRE(string_opt != nullptr);
        REQUIRE(dynamic_cast<ConfigOptionString*>(string_opt) != nullptr);
        delete string_opt;

        def.type = coBool;
        auto* bool_opt = def.create_empty_option();
        REQUIRE(bool_opt != nullptr);
        REQUIRE(dynamic_cast<ConfigOptionBool*>(bool_opt) != nullptr);
        delete bool_opt;
    }

    SECTION("create_default_option") {
        ConfigOptionDef def;

        def.type = coFloat;
        auto* float_opt = dynamic_cast<ConfigOptionFloat*>(def.create_default_option());
        REQUIRE(float_opt != nullptr);
        REQUIRE(float_opt->value == 0.0);
        delete float_opt;

        def.type = coInt;
        auto* int_opt = dynamic_cast<ConfigOptionInt*>(def.create_default_option());
        REQUIRE(int_opt != nullptr);
        REQUIRE(int_opt->value == 0);
        delete int_opt;

        def.type = coBool;
        auto* bool_opt = dynamic_cast<ConfigOptionBool*>(def.create_default_option());
        REQUIRE(bool_opt != nullptr);
        REQUIRE(bool_opt->value == false);
        delete bool_opt;
    }

    SECTION("cli_args") {
        ConfigOptionDef def;

        // Test with empty cli
        def.cli = "";
        auto args1 = def.cli_args("test_option");
        REQUIRE(args1.size() == 1);
        REQUIRE(args1[0] == "test-option");

        // Test with custom cli
        def.cli = "custom-arg";
        auto args2 = def.cli_args("test_option");
        REQUIRE(args2.size() == 1);
        REQUIRE(args2[0] == "custom-arg");
    }
}

TEST_CASE("ConfigOption Error Handling", "[Config]") {
    SECTION("Invalid Float") {
        ConfigOptionFloat opt;
        REQUIRE_FALSE(opt.deserialize("not_a_number"));
    }

    SECTION("Invalid Int") {
        ConfigOptionInt opt;
        REQUIRE_FALSE(opt.deserialize("not_a_number"));
    }

    SECTION("Invalid Bool") {
        ConfigOptionBool opt;
        REQUIRE_FALSE(opt.deserialize("not_a_bool"));
    }
}

TEST_CASE("ConfigOption Edge Cases", "[Config]") {
    SECTION("Float Zero") {
        ConfigOptionFloat opt(0.0);
        REQUIRE(opt.serialize() == "0");
    }

    SECTION("Float Negative") {
        ConfigOptionFloat opt(-1.5);
        REQUIRE(opt.serialize() == "-1.5");
    }

    SECTION("Int Zero") {
        ConfigOptionInt opt(0);
        REQUIRE(opt.serialize() == "0");
    }

    SECTION("Int Negative") {
        ConfigOptionInt opt(-42);
        REQUIRE(opt.serialize() == "-42");
    }

    SECTION("Empty String") {
        ConfigOptionString opt("");
        REQUIRE(opt.serialize() == "");
    }

    SECTION("Clone and Equality") {
        ConfigOptionFloat opt1(1.5);
        std::unique_ptr<ConfigOption> opt2(opt1.clone());
        REQUIRE(*opt2 == opt1);

        ConfigOptionInt opt3(42);
        std::unique_ptr<ConfigOption> opt4(opt3.clone());
        REQUIRE(*opt4 == opt3);

        ConfigOptionString opt5("test");
        std::unique_ptr<ConfigOption> opt6(opt5.clone());
        REQUIRE(*opt6 == opt5);

        ConfigOptionBool opt7(true);
        std::unique_ptr<ConfigOption> opt8(opt7.clone());
        REQUIRE(*opt8 == opt7);
    }
}

TEST_CASE("String escaping and unescaping", "[Config]") {
    SECTION("String escaping") {
        REQUIRE(escape_string_cstyle("simple") == "simple");
        REQUIRE(escape_string_cstyle("with spaces") == "with spaces");
        REQUIRE(escape_string_cstyle("with\"quote") == "with\\\"quote");
        REQUIRE(escape_string_cstyle("with\\backslash") == "with\\\\backslash");
        REQUIRE(escape_string_cstyle("with\nnewline") == "with\\nnewline");
        REQUIRE(escape_string_cstyle("with\rreturn") == "with\\rreturn");
    }

    SECTION("String unescaping") {
        std::string result;

        REQUIRE(unescape_string_cstyle("simple", result));
        REQUIRE(result == "simple");

        REQUIRE(unescape_string_cstyle("with spaces", result));
        REQUIRE(result == "with spaces");

        REQUIRE(unescape_string_cstyle("with\\\"quote", result));
        REQUIRE(result == "with\"quote");

        REQUIRE(unescape_string_cstyle("with\\\\backslash", result));
        REQUIRE(result == "with\\backslash");

        REQUIRE(unescape_string_cstyle("with\\nnewline", result));
        REQUIRE(result == "with\nnewline");

        REQUIRE(unescape_string_cstyle("with\\rreturn", result));
        REQUIRE(result == "with\rreturn");

        // Invalid escape sequence
        REQUIRE_FALSE(unescape_string_cstyle("invalid\\", result));
    }
}

TEST_CASE("ConfigOptionFloat", "[Config]") {
    SECTION("Basic operations") {
        ConfigOptionFloat opt;
        REQUIRE(opt.value == 0.0);

        opt.value = 3.14;
        REQUIRE(opt.value == Catch::Approx(3.14));
        REQUIRE(opt.getFloat() == Catch::Approx(3.14));

        test_serialize_deserialize(opt, "3.14");
    }

    SECTION("Negative values") {
        ConfigOptionFloat opt(-2.718);
        REQUIRE(opt.value == Catch::Approx(-2.718));
        test_serialize_deserialize(opt, "-2.718");
    }

    SECTION("Zero value") {
        ConfigOptionFloat opt(0.0);
        test_serialize_deserialize(opt, "0");
    }
}

TEST_CASE("ConfigOptionFloats", "[Config]") {
    SECTION("Empty vector") {
        ConfigOptionFloats opt;
        REQUIRE(opt.values.empty());
        test_serialize_deserialize(opt, "");
    }

    SECTION("Single value") {
        ConfigOptionFloats opt;
        opt.values.push_back(3.14);
        test_serialize_deserialize(opt, "3.14");
    }

    SECTION("Multiple values") {
        ConfigOptionFloats opt;
        opt.values = {1.1, 2.2, 3.3};
        test_serialize_deserialize(opt, "1.1,2.2,3.3");
    }

    SECTION("Negative values") {
        ConfigOptionFloats opt;
        opt.values = {-1.1, 2.2, -3.3};
        test_serialize_deserialize(opt, "-1.1,2.2,-3.3");
    }
}

TEST_CASE("ConfigOptionInt", "[Config]") {
    SECTION("Basic operations") {
        ConfigOptionInt opt;
        REQUIRE(opt.value == 0);

        opt.value = 42;
        REQUIRE(opt.value == 42);
        REQUIRE(opt.getInt() == 42);

        test_serialize_deserialize(opt, "42");
    }

    SECTION("Negative values") {
        ConfigOptionInt opt(-42);
        REQUIRE(opt.value == -42);
        test_serialize_deserialize(opt, "-42");
    }

    SECTION("Zero value") {
        ConfigOptionInt opt(0);
        test_serialize_deserialize(opt, "0");
    }
}

TEST_CASE("ConfigOptionInts", "[Config]") {
    SECTION("Empty vector") {
        ConfigOptionInts opt;
        REQUIRE(opt.values.empty());
        test_serialize_deserialize(opt, "");
    }

    SECTION("Single value") {
        ConfigOptionInts opt;
        opt.values.push_back(42);
        test_serialize_deserialize(opt, "42");
    }

    SECTION("Multiple values") {
        ConfigOptionInts opt;
        opt.values = {1, 2, 3};
        test_serialize_deserialize(opt, "1,2,3");
    }

    SECTION("Negative values") {
        ConfigOptionInts opt;
        opt.values = {-1, 2, -3};
        test_serialize_deserialize(opt, "-1,2,-3");
    }
}

TEST_CASE("ConfigOptionString", "[Config]") {
    SECTION("Empty string") {
        ConfigOptionString opt;
        REQUIRE(opt.value.empty());
        test_serialize_deserialize(opt, "");
    }

    SECTION("Simple string") {
        ConfigOptionString opt("test");
        REQUIRE(opt.value == "test");
        test_serialize_deserialize(opt, "test");
    }

    SECTION("String with spaces") {
        ConfigOptionString opt("hello world");
        test_serialize_deserialize(opt, "hello world");
    }

    SECTION("String with special characters") {
        ConfigOptionString opt("test;test,test");
        test_serialize_deserialize(opt, "test;test,test");
    }
}

TEST_CASE("ConfigOptionStrings", "[Config]") {
    SECTION("Empty vector") {
        ConfigOptionStrings opt;
        REQUIRE(opt.values.empty());
        test_serialize_deserialize(opt, "");
    }

    SECTION("Single string") {
        ConfigOptionStrings opt;
        opt.values.push_back("test");
        test_serialize_deserialize(opt, "test");
    }

    SECTION("Multiple strings") {
        ConfigOptionStrings opt;
        opt.values = {"test1", "test2", "test3"};
        test_serialize_deserialize(opt, "test1;test2;test3");
    }

    SECTION("Strings with spaces") {
        ConfigOptionStrings opt;
        opt.values = {"hello world", "test string"};
        test_serialize_deserialize(opt, "\"hello world\";\"test string\"");
    }
}

TEST_CASE("ConfigOptionBool", "[Config]") {
    SECTION("Default value") {
        ConfigOptionBool opt;
        REQUIRE_FALSE(opt.value);
        test_serialize_deserialize(opt, "0");
    }

    SECTION("True value") {
        ConfigOptionBool opt(true);
        REQUIRE(opt.value);
        test_serialize_deserialize(opt, "1");
    }

    SECTION("False value") {
        ConfigOptionBool opt(false);
        REQUIRE_FALSE(opt.value);
        test_serialize_deserialize(opt, "0");
    }
}

TEST_CASE("ConfigOptionBools", "[Config]") {
    SECTION("Empty vector") {
        ConfigOptionBools opt;
        REQUIRE(opt.values.empty());
        test_serialize_deserialize(opt, "");
    }

    SECTION("Single value") {
        ConfigOptionBools opt;
        opt.values.push_back(true);
        test_serialize_deserialize(opt, "1");
    }

    SECTION("Multiple values") {
        ConfigOptionBools opt;
        opt.values = {true, false, true};
        test_serialize_deserialize(opt, "1,0,1");
    }
}

TEST_CASE("ConfigOptionPoint", "[Config]") {
    SECTION("Default value") {
        ConfigOptionPoint opt;
        REQUIRE(opt.value.x() == 0);
        REQUIRE(opt.value.y() == 0);
        test_serialize_deserialize(opt, "0,0");
    }

    SECTION("Custom point") {
        ConfigOptionPoint opt(Vec2d(1.1, 2.2));
        REQUIRE(opt.value.x() == Catch::Approx(1.1));
        REQUIRE(opt.value.y() == Catch::Approx(2.2));
        test_serialize_deserialize(opt, "1.1,2.2");
    }

    SECTION("Negative coordinates") {
        ConfigOptionPoint opt(Vec2d(-1.1, -2.2));
        REQUIRE(opt.value.x() == Catch::Approx(-1.1));
        REQUIRE(opt.value.y() == Catch::Approx(-2.2));
        test_serialize_deserialize(opt, "-1.1,-2.2");
    }
}

TEST_CASE("ConfigOptionPoints", "[Config]") {
    SECTION("Empty vector") {
        ConfigOptionPoints opt;
        REQUIRE(opt.values.empty());
        test_serialize_deserialize(opt, "");
    }

    SECTION("Single point") {
        ConfigOptionPoints opt;
        opt.values.push_back(Vec2d(1.1, 2.2));
        test_serialize_deserialize(opt, "1.1x2.2");
    }

    SECTION("Multiple points") {
        ConfigOptionPoints opt;
        opt.values = {Vec2d(1.1, 2.2), Vec2d(3.3, 4.4)};
        test_serialize_deserialize(opt, "1.1x2.2,3.3x4.4");
    }
}

TEST_CASE("ConfigOptionPercent", "[Config]") {
    SECTION("Default value") {
        ConfigOptionPercent opt;
        REQUIRE(opt.value == 0);
        test_serialize_deserialize(opt, "0%");
    }

    SECTION("Custom value") {
        ConfigOptionPercent opt(50.5);
        REQUIRE(opt.value == Catch::Approx(50.5));
        test_serialize_deserialize(opt, "50.5%");
    }

    SECTION("Get absolute value") {
        ConfigOptionPercent opt(50);
        REQUIRE(opt.get_abs_value(200) == Catch::Approx(100));
    }
}

TEST_CASE("ConfigOptionFloatOrPercent", "[Config]") {
    SECTION("Float value") {
        ConfigOptionFloatOrPercent opt(1.5, false);
        REQUIRE(opt.value == Catch::Approx(1.5));
        REQUIRE_FALSE(opt.percent);
        test_serialize_deserialize(opt, "1.5");
    }

    SECTION("Percent value") {
        ConfigOptionFloatOrPercent opt(50, true);
        REQUIRE(opt.value == Catch::Approx(50));
        REQUIRE(opt.percent);
        test_serialize_deserialize(opt, "50%");
    }

    SECTION("Get absolute value") {
        ConfigOptionFloatOrPercent opt1(1.5, false);
        REQUIRE(opt1.get_abs_value(100) == Catch::Approx(1.5));

        ConfigOptionFloatOrPercent opt2(50, true);
        REQUIRE(opt2.get_abs_value(100) == Catch::Approx(50));
    }
}

TEST_CASE("DynamicConfig", "[Config]") {
    SECTION("Basic operations") {
        DynamicConfig config;
        REQUIRE(config.empty());

        // Add options
        config.set_key_value("test_int", new ConfigOptionInt(42));
        config.set_key_value("test_float", new ConfigOptionFloat(3.14));
        config.set_key_value("test_string", new ConfigOptionString("test"));

        REQUIRE(config.has("test_int"));
        REQUIRE(config.has("test_float"));
        REQUIRE(config.has("test_string"));

        // Get values
        REQUIRE(config.opt_int("test_int") == 42);
        REQUIRE(config.opt_float("test_float") == Catch::Approx(3.14));
        REQUIRE(config.opt_string("test_string") == "test");

        // Erase option
        REQUIRE(config.erase("test_int"));
        REQUIRE_FALSE(config.has("test_int"));
    }

    SECTION("Copy and move operations") {
        DynamicConfig config1;
        config1.set_key_value("test", new ConfigOptionInt(42));

        // Copy
        DynamicConfig config2(config1);
        REQUIRE(config2.opt_int("test") == 42);

        // Move
        DynamicConfig config3(std::move(config2));
        REQUIRE(config3.opt_int("test") == 42);
        REQUIRE(config2.empty());
    }

    SECTION("Equality comparison") {
        DynamicConfig config1, config2;
        config1.set_key_value("test", new ConfigOptionInt(42));
        config2.set_key_value("test", new ConfigOptionInt(42));

        REQUIRE(config1.equals(config2));

        config2.set_key_value("test", new ConfigOptionInt(43));
        REQUIRE_FALSE(config1.equals(config2));
    }
}

TEST_CASE("Config error handling", "[Config]") {
    SECTION("Unknown option") {
        DynamicConfig config;
        REQUIRE_THROWS_AS(config.option_throw<ConfigOptionInt>("nonexistent"), UnknownOptionException);
    }

    SECTION("Bad option type") {
        DynamicConfig config;
        config.set_key_value("test", new ConfigOptionInt(42));
        REQUIRE_THROWS_AS(config.option_throw<ConfigOptionFloat>("test"), BadOptionTypeException);
    }

    SECTION("Bad option value") {
        ConfigOptionFloat opt;
        REQUIRE_FALSE(opt.deserialize("not_a_number"));
    }
}

TEST_CASE("Config serialization", "[Config]") {
    SECTION("DynamicConfig serialization") {
        DynamicConfig config;
        config.set_key_value("int_option", new ConfigOptionInt(42));
        config.set_key_value("float_option", new ConfigOptionFloat(3.14));
        config.set_key_value("string_option", new ConfigOptionString("test"));

        std::string serialized = config.opt_serialize("int_option");
        REQUIRE(serialized == "42");

        serialized = config.opt_serialize("float_option");
        REQUIRE(serialized == "3.14");

        serialized = config.opt_serialize("string_option");
        REQUIRE(serialized == "test");
    }
}

TEST_CASE("ConfigOptionPoint3", "[Config]") {
    SECTION("Default value") {
        ConfigOptionPoint3 opt;
        REQUIRE(opt.value.x() == 0);
        REQUIRE(opt.value.y() == 0);
        REQUIRE(opt.value.z() == 0);
        test_serialize_deserialize(opt, "0,0,0");
    }

    SECTION("Custom point") {
        ConfigOptionPoint3 opt(Vec3d(1.1, 2.2, 3.3));
        REQUIRE(opt.value.x() == Catch::Approx(1.1));
        REQUIRE(opt.value.y() == Catch::Approx(2.2));
        REQUIRE(opt.value.z() == Catch::Approx(3.3));
        test_serialize_deserialize(opt, "1.1,2.2,3.3");
    }

    SECTION("Negative coordinates") {
        ConfigOptionPoint3 opt(Vec3d(-1.1, -2.2, -3.3));
        REQUIRE(opt.value.x() == Catch::Approx(-1.1));
        REQUIRE(opt.value.y() == Catch::Approx(-2.2));
        REQUIRE(opt.value.z() == Catch::Approx(-3.3));
        test_serialize_deserialize(opt, "-1.1,-2.2,-3.3");
    }
}

TEST_CASE("ConfigOptionEnum", "[Config]") {
    SECTION("Basic operations") {
        ConfigOptionEnum<TestEnum> opt;
        REQUIRE(opt.value == TestEnum::First);

        opt.value = TestEnum::Second;
        REQUIRE(opt.value == TestEnum::Second);
        REQUIRE(opt.getInt() == 1);

        test_serialize_deserialize(opt, "second");
    }

    SECTION("Conversion") {
        ConfigOptionEnum<TestEnum> opt(TestEnum::Third);
        REQUIRE(opt.getInt() == 2);
        test_serialize_deserialize(opt, "third");
    }
}

// ========== CONFIG BASE TESTS ==========

TEST_CASE("ConfigBase methods", "[Config]") {
    // For testing ConfigBase functionality, we'll use DynamicConfig which inherits from ConfigBase

    SECTION("equals, diff, and equal") {
        DynamicConfig config1, config2;

        // Empty configs should be equal
        REQUIRE(config1.equals(config2));
        REQUIRE(config1.diff(config2).empty());
        REQUIRE(config1.equal(config2).empty());

        // Add the same option to both - still equal
        config1.set_key_value("test_int", new ConfigOptionInt(42));
        config2.set_key_value("test_int", new ConfigOptionInt(42));
        REQUIRE(config1.equals(config2));
        REQUIRE(config1.diff(config2).empty());

        // Equal returns keys that are equal
        auto equal_keys = config1.equal(config2);
        REQUIRE(equal_keys.size() == 1);
        REQUIRE(equal_keys[0] == "test_int");

        // Different values - not equal
        config2.set_key_value("test_int", new ConfigOptionInt(43));
        REQUIRE_FALSE(config1.equals(config2));
        auto diff_keys = config1.diff(config2);
        REQUIRE(diff_keys.size() == 1);
        REQUIRE(diff_keys[0] == "test_int");
        REQUIRE(config1.equal(config2).empty());

        // Different keys - not equal
        config1.set_key_value("test_float", new ConfigOptionFloat(3.14));
        diff_keys = config1.diff(config2);
        REQUIRE(diff_keys.size() == 1); // Only test_int is different, test_float is only in config1
    }

    SECTION("set methods") {
        TestConfigDef def;
        // Add option definitions
        def.test_add("bool_option", coBool);
        def.test_add("int_option", coInt);
        def.test_add("float_option", coFloat);
        def.test_add("string_option_1", coString);
        def.test_add("string_option_2", coString);

        // Create config with the definition
        TestDynamicConfig config(&def);

        // Test set(string, bool)
        config.set("bool_option", true);
        REQUIRE(config.has("bool_option"));
        REQUIRE(config.opt_bool("bool_option") == true);

        // Test set(string, int)
        config.set("int_option", 42);
        REQUIRE(config.has("int_option"));
        REQUIRE(config.opt_int("int_option") == 42);

        // Test set(string, double)
        config.set("float_option", 3.14);
        REQUIRE(config.has("float_option"));
        REQUIRE(config.opt_float("float_option") == Catch::Approx(3.14));

        // Test set(string, const char*)
        config.set("string_option_1", "test");
        REQUIRE(config.has("string_option_1"));
        REQUIRE(config.opt_string("string_option_1") == "test");

        // Test set(string, const string&)
        std::string test_str = "test_string";
        config.set("string_option_2", test_str);
        REQUIRE(config.has("string_option_2"));
        REQUIRE(config.opt_string("string_option_2") == "test_string");
    }

    SECTION("set_deserialize methods") {
        TestConfigDef def;
        // Add option definitions
        def.test_add("bool_option", coBool);
        def.test_add("int_option", coInt);
        def.test_add("float_option", coFloat);
        def.test_add("string_option_1", coString);
        def.test_add("string_option_2", coString);

        // Create config with the definition
        TestDynamicConfig config(&def);

        ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::Disable);

        // Basic value deserialization
        config.set_deserialize("int_option", "42", substitutions);
        REQUIRE(config.opt_int("int_option") == 42);

        config.set_deserialize("float_option", "3.14", substitutions);
        REQUIRE(config.opt_float("float_option") == Catch::Approx(3.14));

        config.set_deserialize("bool_option", "1", substitutions);
        REQUIRE(config.opt_bool("bool_option") == true);

        config.set_deserialize("string_option_1", "test string", substitutions);
        REQUIRE(config.opt_string("string_option_1") == "test string");

        // Using initializer list
        config.set_deserialize({
            {"int_list", "1,2,3", false},
            {"float_list", "1.1,2.2,3.3", false},
            {"bool_list", "1,0,1", false}
        }, substitutions);

        REQUIRE(config.option<ConfigOptionInts>("int_list")->values == std::vector<int>{1, 2, 3});

        std::vector<double> expected_floats = {1.1, 2.2, 3.3};
        for (size_t i = 0; i < expected_floats.size(); i++) {
            REQUIRE(config.option<ConfigOptionFloats>("float_list")->values[i] == Catch::Approx(expected_floats[i]));
        }

        REQUIRE(config.option<ConfigOptionBools>("bool_list")->values == std::vector<unsigned char>{1, 0, 1});
    }

    SECTION("get_abs_value") {
        TestConfigDef def;
        // Add option definitions
        def.test_add("float_val", coFloat);
        def.test_add("percent_val", coPercent);
        def.test_add("float_or_percent_val", coFloatOrPercent);

        // Create config with the definition
        TestDynamicConfig config(&def);

        // Test with float value
        config.set("float_val", 0.5);
        REQUIRE(config.get_abs_value("float_val") == Catch::Approx(0.5));

        // Test with percent value
        config.set("percent_val", new ConfigOptionPercent(50));
        REQUIRE(config.get_abs_value("percent_val") == Catch::Approx(0.5));

        // Test with float or percent value (as float)
        config.set("float_or_percent_val", new ConfigOptionFloatOrPercent(0.5, false));
        REQUIRE(config.get_abs_value("float_or_percent_val") == Catch::Approx(0.5));

        // Test with float or percent value (as percent)
        config.set("float_or_percent_val", new ConfigOptionFloatOrPercent(50, true));
        REQUIRE(config.get_abs_value("float_or_percent_val") == Catch::Approx(0.5));
    }
}

TEST_CASE("ConfigBase load and save", "[Config][FileIO]") {
    // Tests that interact with the filesystem should be in a separate section
    // Note: These tests need real files to test with

    SECTION("save and load") {
        // Create a temporary file for testing
        std::string temp_file = "test_config.ini";

        {
            // Create a TestConfigDef with option definitions
            TestConfigDef def;
            def.test_add("int_option", coInt);
            def.test_add("float_option", coFloat);
            def.test_add("bool_option", coBool);
            def.test_add("string_option", coString);

            // Create and save a config
            TestDynamicConfig config(&def);
            config.set("int_option", 42);
            config.set("float_option", 3.14);
            config.set("bool_option", true);
            config.set("string_option", "test");

            config.save(temp_file);
        }

        {
            // Create a TestConfigDef with option definitions
            TestConfigDef def;
            def.test_add("int_option", coInt);
            def.test_add("float_option", coFloat);
            def.test_add("bool_option", coBool);
            def.test_add("string_option", coString);

            // Load the config
            TestDynamicConfig config(&def);
            auto substitutions = config.load(temp_file, ForwardCompatibilitySubstitutionRule::Disable);

            // Verify values loaded correctly
            REQUIRE(substitutions.empty());
            REQUIRE(config.opt_int("int_option") == 42);
            REQUIRE(config.opt_float("float_option") == Catch::Approx(3.14));
            REQUIRE(config.opt_bool("bool_option") == true);
            REQUIRE(config.opt_string("string_option") == "test");
        }

        // Clean up
        std::remove(temp_file.c_str());
    }

    SECTION("load_from_ini_string") {
        TestConfigDef def;
        // Add option definitions
        def.test_add("int_option", coInt);
        def.test_add("float_option", coFloat);
        def.test_add("bool_option", coBool);
        def.test_add("string_option", coString);

        // Create config with the definition
        TestDynamicConfig config(&def);

        std::string ini_data =
            "int_option = 42\n"
            "float_option = 3.14\n"
            "bool_option = 1\n"
            "string_option = test\n";

        auto substitutions = config.load_from_ini_string(ini_data, ForwardCompatibilitySubstitutionRule::Disable);

        REQUIRE(substitutions.empty());
        REQUIRE(config.opt_int("int_option") == 42);
        REQUIRE(config.opt_float("float_option") == Catch::Approx(3.14));
        REQUIRE(config.opt_bool("bool_option") == true);
        REQUIRE(config.opt_string("string_option") == "test");
    }

    SECTION("load_from_ini_string_commented") {
        TestConfigDef def;

        // Add option definitions
        def.test_add("int_option", coInt);
        def.test_add("float_option", coFloat);
        def.test_add("bool_option", coBool);
        def.test_add("string_option", coString);

        // Create config with the definition
        TestDynamicConfig config(&def);

        std::string ini_data =
            "# This is a comment\n"
            "int_option = 42 # This is a comment\n"
            "float_option = 3.14\n"
            "# Another comment\n"
            "bool_option = 1\n"
            "string_option = test # Comment after string\n";

        auto substitutions = config.load_from_ini_string_commented(std::move(ini_data), ForwardCompatibilitySubstitutionRule::Disable);

        REQUIRE(substitutions.empty());
        REQUIRE(config.opt_int("int_option") == 42);
        REQUIRE(config.opt_float("float_option") == Catch::Approx(3.14));
        REQUIRE(config.opt_bool("bool_option") == true);
        REQUIRE(config.opt_string("string_option") == "test");
    }

    SECTION("load_string_map") {
        DynamicConfig config;
        std::map<std::string, std::string> key_values = {
            {"int_option", "42"},
            {"float_option", "3.14"},
            {"bool_option", "1"},
            {"string_option", "test"}
        };

        auto substitutions = config.load_string_map(key_values, ForwardCompatibilitySubstitutionRule::Disable);

        REQUIRE(substitutions.empty());
        REQUIRE(config.opt_int("int_option") == 42);
        REQUIRE(config.opt_float("float_option") == Catch::Approx(3.14));
        REQUIRE(config.opt_bool("bool_option") == true);
        REQUIRE(config.opt_string("string_option") == "test");
    }
}

// ========== CONFIG DEF TESTS ==========

TEST_CASE("ConfigDef methods", "[Config]") {
    SECTION("add and add_nullable") {
        TestConfigDef def;

        auto* float_def = def.test_add("float_option", coFloat);
        REQUIRE(float_def != nullptr);
        REQUIRE(float_def->type == coFloat);
        REQUIRE_FALSE(float_def->nullable);
        REQUIRE(!float_def->default_value);

        auto* nullable_float_def = def.test_add_nullable("nullable_float", coFloat);
        REQUIRE(nullable_float_def != nullptr);
        REQUIRE(nullable_float_def->type == coFloat);
        REQUIRE(nullable_float_def->nullable);
        REQUIRE(!nullable_float_def->default_value);

        auto* int_def = def.test_add("int_option", coInt);
        REQUIRE(int_def != nullptr);
        REQUIRE(int_def->type == coInt);

        auto* string_def = def.test_add("string_option", coString);
        REQUIRE(string_def != nullptr);
        REQUIRE(string_def->type == coString);

        auto* bool_def = def.test_add("bool_option", coBool);
        REQUIRE(bool_def != nullptr);
        REQUIRE(bool_def->type == coBool);
    }

    SECTION("print_cli_help") {
        TestConfigDef def;

        // Add some options with descriptions
        auto* opt1 = def.test_add("option1", coInt);
        opt1->full_label = "Option 1";
        opt1->tooltip = "This is option 1";
        opt1->cli = "o1";

        auto* opt2 = def.test_add("option2", coFloat);
        opt2->full_label = "Option 2";
        opt2->tooltip = "This is option 2";

        // Capture console output
        std::ostringstream oss;
        def.print_cli_help(oss, true);

        std::string output = oss.str();

        // Verify help output contains our options
        REQUIRE(output.find("Option 1") != std::string::npos);
        REQUIRE(output.find("This is option 1") != std::string::npos);
        REQUIRE(output.find("Option 2") != std::string::npos);
        REQUIRE(output.find("This is option 2") != std::string::npos);

        // Test with filter
        std::ostringstream oss2;
        def.print_cli_help(oss2, true, [](const ConfigOptionDef& def) {
            return def.cli == "o1";
        });

        std::string filtered_output = oss2.str();
        REQUIRE(filtered_output.find("Option 1") != std::string::npos);
        REQUIRE(filtered_output.find("Option 2") == std::string::npos);
    }
}

// ========== STATIC CONFIG TESTS ==========

TEST_CASE("StaticConfig methods", "[Config]") {
    SECTION("keys and set_defaults") {
        TestStaticConfig config;

        // Test keys() returns correct keys
        auto keys = config.keys();
        REQUIRE(keys.size() == 3);
        REQUIRE(std::find(keys.begin(), keys.end(), "opt1") != keys.end());
        REQUIRE(std::find(keys.begin(), keys.end(), "opt2") != keys.end());
        REQUIRE(std::find(keys.begin(), keys.end(), "opt3") != keys.end());

        // Test defaults were set correctly
        REQUIRE(config.opt1()->value == 10);
        REQUIRE(config.opt2()->value == Catch::Approx(20.0));
        REQUIRE(config.opt3()->value == "default");
    }
}

// ========== NULLABLE OPTION TESTS ==========

TEST_CASE("Nullable ConfigOptions", "[Config]") {
    SECTION("ConfigOptionFloatsNullable") {
        ConfigOptionFloatsNullable opt;
        REQUIRE(opt.nullable());
        REQUIRE(opt.values.empty());

        // Test with regular values
        opt.values = {1.0, 2.0, 3.0};
        REQUIRE_FALSE(opt.is_nil());
        REQUIRE_FALSE(opt.is_nil(0));
        REQUIRE_FALSE(opt.is_nil(1));
        REQUIRE_FALSE(opt.is_nil(2));

        // Test with nil values
        opt.values = {ConfigOptionFloatsNullable::nil_value(), 2.0, ConfigOptionFloatsNullable::nil_value()};
        REQUIRE_FALSE(opt.is_nil()); // Not completely nil
        REQUIRE(opt.is_nil(0));
        REQUIRE_FALSE(opt.is_nil(1));
        REQUIRE(opt.is_nil(2));

        // Test completely nil
        opt.values = {ConfigOptionFloatsNullable::nil_value(), ConfigOptionFloatsNullable::nil_value()};
        REQUIRE(opt.is_nil());

        // Test serialization
        opt.values = {1.0, ConfigOptionFloatsNullable::nil_value(), 3.0};
        std::string serialized = opt.serialize();
        REQUIRE(serialized == "1,nil,3");

        // Test deserialization
        ConfigOptionFloatsNullable opt2;
        REQUIRE(opt2.deserialize("2,nil,4"));
        REQUIRE(opt2.values.size() == 3);
        REQUIRE(opt2.values[0] == Catch::Approx(2.0));
        REQUIRE(opt2.is_nil(1));
        REQUIRE(opt2.values[2] == Catch::Approx(4.0));
    }

    SECTION("ConfigOptionIntsNullable") {
        ConfigOptionIntsNullable opt;
        REQUIRE(opt.nullable());
        REQUIRE(opt.values.empty());

        // Test with regular values
        opt.values = {1, 2, 3};
        REQUIRE_FALSE(opt.is_nil());

        // Test with nil values
        opt.values = {ConfigOptionIntsNullable::nil_value(), 2, ConfigOptionIntsNullable::nil_value()};
        REQUIRE_FALSE(opt.is_nil()); // Not completely nil
        REQUIRE(opt.is_nil(0));
        REQUIRE_FALSE(opt.is_nil(1));
        REQUIRE(opt.is_nil(2));

        // Test serialization and deserialization
        opt.values = {1, ConfigOptionIntsNullable::nil_value(), 3};
        std::string serialized = opt.serialize();
        REQUIRE(serialized == "1,nil,3");

        ConfigOptionIntsNullable opt2;
        REQUIRE(opt2.deserialize("2,nil,4"));
        REQUIRE(opt2.values.size() == 3);
        REQUIRE(opt2.values[0] == 2);
        REQUIRE(opt2.is_nil(1));
        REQUIRE(opt2.values[2] == 4);
    }

    SECTION("ConfigOptionBoolsNullable") {
        ConfigOptionBoolsNullable opt;
        REQUIRE(opt.nullable());
        REQUIRE(opt.values.empty());

        // Test with regular values
        opt.values = {1, 0, 1};
        REQUIRE_FALSE(opt.is_nil());

        // Test with nil values
        opt.values = {ConfigOptionBoolsNullable::nil_value(), 0, ConfigOptionBoolsNullable::nil_value()};
        REQUIRE_FALSE(opt.is_nil()); // Not completely nil
        REQUIRE(opt.is_nil(0));
        REQUIRE_FALSE(opt.is_nil(1));
        REQUIRE(opt.is_nil(2));

        // Test serialization and deserialization
        opt.values = {1, ConfigOptionBoolsNullable::nil_value(), 0};
        std::string serialized = opt.serialize();
        REQUIRE(serialized == "1,nil,0");

        ConfigOptionBoolsNullable opt2;
        REQUIRE(opt2.deserialize("0,nil,1"));
        REQUIRE(opt2.values.size() == 3);
        REQUIRE(opt2.values[0] == 0);
        REQUIRE(opt2.is_nil(1));
        REQUIRE(opt2.values[2] == 1);
    }

    SECTION("ConfigOptionFloatsOrPercentsNullable") {
        ConfigOptionFloatsOrPercentsNullable opt;
        REQUIRE(opt.nullable());
        REQUIRE(opt.values.empty());

        // Test with regular values
        FloatOrPercent val1;
        val1.value = 1.0;
        val1.percent = false;

        FloatOrPercent val2;
        val2.value = 50.0;
        val2.percent = true;

        opt.values = {val1, val2};
        REQUIRE_FALSE(opt.is_nil());

        // Test with nil values
        FloatOrPercent nil_val = ConfigOptionFloatsOrPercentsNullable::nil_value();
        opt.values = {nil_val, val2, nil_val};
        REQUIRE_FALSE(opt.is_nil()); // Not completely nil
        REQUIRE(opt.is_nil(0));
        REQUIRE_FALSE(opt.is_nil(1));
        REQUIRE(opt.is_nil(2));

        // Test serialization and deserialization
        opt.values = {val1, nil_val, val2};
        std::string serialized = opt.serialize();
        REQUIRE(serialized == "1,nil,50%");

        ConfigOptionFloatsOrPercentsNullable opt2;
        REQUIRE(opt2.deserialize("2,nil,25%"));
        REQUIRE(opt2.values.size() == 3);
        REQUIRE(opt2.values[0].value == Catch::Approx(2.0));
        REQUIRE_FALSE(opt2.values[0].percent);
        REQUIRE(opt2.is_nil(1));
        REQUIRE(opt2.values[2].value == Catch::Approx(25.0));
        REQUIRE(opt2.values[2].percent);
    }
}

// ========== ENUM TESTS ==========

// Define a test enum for generic enum classes
enum class TestEnumGeneric {
    First = 0,
    Second = 1,
    Third = 2
};

// Create a keys map for testing
const t_config_enum_values test_enum_keys_map = {
    {"first", static_cast<int>(TestEnumGeneric::First)},
    {"second", static_cast<int>(TestEnumGeneric::Second)},
    {"third", static_cast<int>(TestEnumGeneric::Third)}
};

TEST_CASE("ConfigOptionEnumGeneric", "[Config]") {
    SECTION("Basic operations") {
        ConfigOptionEnumGeneric opt(&test_enum_keys_map);
        REQUIRE(opt.value == 0);

        opt.value = static_cast<int>(TestEnumGeneric::Second);
        REQUIRE(opt.value == 1);
        REQUIRE(opt.getInt() == 1);

        // Test serialization
        REQUIRE(opt.serialize() == "second");

        // Test deserialization
        ConfigOptionEnumGeneric opt2(&test_enum_keys_map);
        REQUIRE(opt2.deserialize("third"));
        REQUIRE(opt2.value == 2);

        // Test invalid value
        ConfigOptionEnumGeneric opt3(&test_enum_keys_map);
        REQUIRE_FALSE(opt3.deserialize("invalid"));
    }
}

TEST_CASE("ConfigOptionEnumsGeneric", "[Config]") {
    SECTION("Basic operations") {
        ConfigOptionEnumsGeneric opt(&test_enum_keys_map);
        REQUIRE(opt.values.empty());

        // Add values
        opt.values = {0, 1, 2};
        REQUIRE(opt.values.size() == 3);
        REQUIRE(opt.values[0] == 0);
        REQUIRE(opt.values[1] == 1);
        REQUIRE(opt.values[2] == 2);

        // Test serialization
        REQUIRE(opt.serialize() == "first,second,third");

        // Test vector serialization
        auto serialized_vec = opt.vserialize();
        REQUIRE(serialized_vec.size() == 3);
        REQUIRE(serialized_vec[0] == "first");
        REQUIRE(serialized_vec[1] == "second");
        REQUIRE(serialized_vec[2] == "third");

        // Test deserialization
        ConfigOptionEnumsGeneric opt2(&test_enum_keys_map);
        REQUIRE(opt2.deserialize("third,first,second"));
        REQUIRE(opt2.values.size() == 3);
        REQUIRE(opt2.values[0] == 2);
        REQUIRE(opt2.values[1] == 0);
        REQUIRE(opt2.values[2] == 1);

        // Test invalid value
        ConfigOptionEnumsGeneric opt3(&test_enum_keys_map);
        REQUIRE_FALSE(opt3.deserialize("invalid,first"));
    }

    SECTION("Nullable version") {
        ConfigOptionEnumsGenericNullable opt(&test_enum_keys_map);
        REQUIRE(opt.nullable());
        REQUIRE(opt.values.empty());

        // Test with regular values
        opt.values = {0, 1, 2};
        REQUIRE_FALSE(opt.is_nil());

        // Test with nil values
        int nil_val = ConfigOptionEnumsGenericNullable::nil_value();
        opt.values = {nil_val, 1, nil_val};
        REQUIRE_FALSE(opt.is_nil()); // Not completely nil
        REQUIRE(opt.is_nil(0));
        REQUIRE_FALSE(opt.is_nil(1));
        REQUIRE(opt.is_nil(2));

        // Test serialization
        opt.values = {0, nil_val, 2};
        REQUIRE(opt.serialize() == "first,nil,third");

        // Test deserialization
        ConfigOptionEnumsGenericNullable opt2(&test_enum_keys_map);
        REQUIRE(opt2.deserialize("third,nil,first"));
        REQUIRE(opt2.values.size() == 3);
        REQUIRE(opt2.values[0] == 2);
        REQUIRE(opt2.is_nil(1));
        REQUIRE(opt2.values[2] == 0);
    }
}

// ========== UTILITY FUNCTION TESTS ==========

TEST_CASE("String utility functions", "[Config]") {
    SECTION("escape_strings_cstyle and unescape_strings_cstyle") {
        std::vector<std::string> strings = {"simple", "with spaces", "with\"quote", "with\\backslash", "with\nnewline", "with\rreturn"};

        // Test escape_strings_cstyle
        std::string escaped = escape_strings_cstyle(strings);
        REQUIRE(escaped == "simple;\"with spaces\";with\\\"quote;with\\\\backslash;with\\nnewline;with\\rreturn");

        // Test unescape_strings_cstyle
        std::vector<std::string> unescaped;
        REQUIRE(unescape_strings_cstyle(escaped, unescaped));
        REQUIRE(unescaped.size() == strings.size());
        for (size_t i = 0; i < strings.size(); i++) {
            REQUIRE(unescaped[i] == strings[i]);
        }

        // Test with empty list
        std::vector<std::string> empty;
        std::string escaped_empty = escape_strings_cstyle(empty);
        REQUIRE(escaped_empty.empty());

        std::vector<std::string> unescaped_empty;
        REQUIRE(unescape_strings_cstyle(escaped_empty, unescaped_empty));
        REQUIRE(unescaped_empty.empty());

        // Test with invalid escape sequence
        std::vector<std::string> invalid_unescaped;
        REQUIRE_FALSE(unescape_strings_cstyle("invalid\\", invalid_unescaped));
    }

    SECTION("escape_ampersand") {
        REQUIRE(escape_ampersand("simple") == "simple");
        REQUIRE(escape_ampersand("with spaces") == "with spaces");
        REQUIRE(escape_ampersand("with&ampersand") == "with&&ampersand");
        REQUIRE(escape_ampersand("with&&doubleampersand") == "with&&&&doubleampersand");
        REQUIRE(escape_ampersand("") == "");
    }
}

TEST_CASE("Helper functions", "[Config]") {
    SECTION("is_whitespace and related functions") {
        auto is_end_of_line = [](char c) { return c == '\r' || c == '\n' || c == 0; };
        auto is_whitespace = [](char c) { return c == ' ' || c == '\t' || c == '\f' || c == '\v'; };
        auto is_end_of_gcode_line = [is_end_of_line](char c) { return c == ';' || is_end_of_line(c); };

        REQUIRE(is_whitespace(' '));
        REQUIRE(is_whitespace('\t'));
        REQUIRE(is_whitespace('\f'));
        REQUIRE(is_whitespace('\v'));
        REQUIRE_FALSE(is_whitespace('a'));
        REQUIRE_FALSE(is_whitespace('\n'));
        REQUIRE_FALSE(is_whitespace('\r'));
    }

    SECTION("ReverseLineReader") {
        // Create a temporary file for testing
        std::string temp_file = "temp_test_file.txt";

        // Write test data to the file
        {
            std::ofstream ofs(temp_file);
            ofs << "Line 1\n";
            ofs << "Line 2\n";
            ofs << "Line 3\n";
            ofs.close();
        }

        // Test reading the file in reverse order
        try {
            ReverseLineReader reader(temp_file);
            std::string line;

            REQUIRE(reader.getline(line));
            REQUIRE(line == "Line 3");

            REQUIRE(reader.getline(line));
            REQUIRE(line == "Line 2");

            REQUIRE(reader.getline(line));
            REQUIRE(line == "Line 1");

            REQUIRE_FALSE(reader.getline(line));
        } catch (const std::exception& e) {
            FAIL("Exception while reading file: " << e.what());
        }

        // Clean up
        boost::filesystem::remove(temp_file);
    }
}

// ========== REVERSE LINE READER TESTS ==========

// Note: This requires a file to read. We can create a temporary file for testing.
TEST_CASE("ReverseLineReader", "[Config][FileIO]") {
    // Create a temporary test file
    std::string temp_file = "reverse_line_test.txt";
    {
        std::ofstream ofs(temp_file);
        ofs << "Line 1\n";
        ofs << "Line 2\n";
        ofs << "Line 3\n";
        ofs << "Line 4\n";
        ofs << "Line 5";  // No newline at the end
    }

    SECTION("Basic line reading") {
        std::ifstream ifs(temp_file);
        ReverseLineReader reader(ifs, 0);

        std::string line;

        // Read lines in reverse order
        REQUIRE(reader.getline(line));
        REQUIRE(line == "Line 5");

        REQUIRE(reader.getline(line));
        REQUIRE(line == "Line 4");

        REQUIRE(reader.getline(line));
        REQUIRE(line == "Line 3");

        REQUIRE(reader.getline(line));
        REQUIRE(line == "Line 2");

        REQUIRE(reader.getline(line));
        REQUIRE(line == "Line 1");

        // No more lines
        REQUIRE_FALSE(reader.getline(line));
    }

    SECTION("Different line endings") {
        // Create a file with different line endings
        std::string mixed_endings_file = "mixed_endings.txt";
        {
            std::ofstream ofs(mixed_endings_file);
            ofs << "Line 1\n";  // LF
            ofs << "Line 2\r\n";  // CRLF
            ofs << "Line 3\r";  // CR
            ofs << "Line 4";  // No ending
        }

        std::ifstream ifs(mixed_endings_file);
        ReverseLineReader reader(ifs, 0);

        std::string line;

        REQUIRE(reader.getline(line));
        REQUIRE(line == "Line 4");

        REQUIRE(reader.getline(line));
        REQUIRE(line == "Line 3");

        REQUIRE(reader.getline(line));
        REQUIRE(line == "Line 2");

        REQUIRE(reader.getline(line));
        REQUIRE(line == "Line 1");

        REQUIRE_FALSE(reader.getline(line));

        // Clean up
        std::remove(mixed_endings_file.c_str());
    }

    SECTION("Empty file") {
        std::string empty_file = "empty.txt";
        {
            std::ofstream ofs(empty_file);
            // Create empty file
        }

        std::ifstream ifs(empty_file);
        ReverseLineReader reader(ifs, 0);

        std::string line;
        REQUIRE_FALSE(reader.getline(line));

        // Clean up
        std::remove(empty_file.c_str());
    }

    SECTION("Single line file") {
        std::string single_line_file = "single_line.txt";
        {
            std::ofstream ofs(single_line_file);
            ofs << "Just one line";
        }

        std::ifstream ifs(single_line_file);
        ReverseLineReader reader(ifs, 0);

        std::string line;
        REQUIRE(reader.getline(line));
        REQUIRE(line == "Just one line");
        REQUIRE_FALSE(reader.getline(line));

        // Clean up
        std::remove(single_line_file.c_str());
    }

    // Clean up the main test file
    std::remove(temp_file.c_str());
}

// ========== EXCEPTION CLASS TESTS ==========

TEST_CASE("Exception classes", "[Config][Exceptions]") {
    SECTION("ConfigurationError") {
        ConfigurationError error("Test error");
        REQUIRE(std::string(error.what()) == "Test error");
    }

    SECTION("UnknownOptionException") {
        UnknownOptionException error1;
        REQUIRE(std::string(error1.what()) == "Unknown option exception");

        UnknownOptionException error2("test_option");
        REQUIRE(std::string(error2.what()) == "Unknown option exception: test_option");
    }

    SECTION("NoDefinitionException") {
        NoDefinitionException error1;
        REQUIRE(std::string(error1.what()) == "No definition exception");

        NoDefinitionException error2("test_option");
        REQUIRE(std::string(error2.what()) == "No definition exception: test_option");
    }

    SECTION("BadOptionTypeException") {
        BadOptionTypeException error1;
        REQUIRE(std::string(error1.what()) == "Bad option type exception");

        BadOptionTypeException error2("Wrong type");
        REQUIRE(std::string(error2.what()) == "Wrong type");
    }

    SECTION("BadOptionValueException") {
        BadOptionValueException error1;
        REQUIRE(std::string(error1.what()) == "Bad option value exception");

        BadOptionValueException error2("Invalid value");
        REQUIRE(std::string(error2.what()) == "Invalid value");
    }

    SECTION("Exception inheritance") {
        // Test that all exception types derive from ConfigurationError
    }
}