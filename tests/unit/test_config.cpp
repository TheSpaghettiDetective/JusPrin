#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include "libslic3r/Config.hpp"

using namespace Slic3r;

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

TEST_CASE("ConfigOption Append Tests", "[Config]") {
    SECTION("String Append") {
        ConfigOptionString opt("Hello");
        REQUIRE(opt.deserialize(" World", true));
        REQUIRE(opt.value == "Hello World");

        REQUIRE(opt.deserialize("!", true));
        REQUIRE(opt.value == "Hello World!");
    }

    SECTION("Scalar Types Don't Support Append") {
        ConfigOptionFloat float_opt(1.5);
        REQUIRE_FALSE(float_opt.deserialize("2.5", true));
        REQUIRE(float_opt.value == 1.5);
    }
}

TEST_CASE("ConfigOptionString operations", "[Config]") {
    SECTION("Basic string operations") {
        ConfigOptionString opt;
        REQUIRE(opt.value.empty());

        opt.value = "test";
        REQUIRE(opt.value == "test");

        std::string serialized = opt.serialize();
        REQUIRE(serialized == "test");
    }

    SECTION("String deserialization") {
        ConfigOptionString opt;
        REQUIRE(opt.deserialize("simple"));
        REQUIRE(opt.value == "simple");

        REQUIRE(opt.deserialize("with spaces"));
        REQUIRE(opt.value == "with spaces");

        REQUIRE(opt.deserialize("with\"quote"));
        REQUIRE(opt.value == "with\"quote");

        REQUIRE(opt.deserialize("with\\escape"));
        REQUIRE(opt.value == "with\\escape");
    }

    SECTION("String append operations") {
        ConfigOptionString opt;
        opt.value = "Hello";

        REQUIRE(opt.deserialize(" World", true));
        REQUIRE(opt.value == "Hello World");

        REQUIRE(opt.deserialize("!", true));
        REQUIRE(opt.value == "Hello World!");
    }
}

TEST_CASE("ConfigOptionInt operations", "[Config]") {
    SECTION("Basic integer operations") {
        ConfigOptionInt opt;
        REQUIRE(opt.value == 0);

        opt.value = 42;
        REQUIRE(opt.value == 42);

        std::string serialized = opt.serialize();
        REQUIRE(serialized == "42");
    }

    SECTION("Integer deserialization") {
        ConfigOptionInt opt;
        REQUIRE(opt.deserialize("42"));
        REQUIRE(opt.value == 42);

        REQUIRE(opt.deserialize("-123"));
        REQUIRE(opt.value == -123);

        REQUIRE_FALSE(opt.deserialize("not_a_number"));
        REQUIRE(opt.value == -123);  // Value should remain unchanged
    }

    SECTION("Integer append operations") {
        ConfigOptionInt opt;
        opt.value = 42;

        // Append should not be supported for integers
        REQUIRE_FALSE(opt.deserialize("1", true));
        REQUIRE(opt.value == 42);  // Value should remain unchanged
    }
}

TEST_CASE("ConfigOptionFloat operations", "[Config]") {
    SECTION("Basic float operations") {
        ConfigOptionFloat opt;
        REQUIRE(opt.value == Catch::Approx(0.0));

        opt.value = 3.14159;
        REQUIRE(opt.value == Catch::Approx(3.14159));

        std::string serialized = opt.serialize();
        REQUIRE(serialized == "3.14159");
    }

    SECTION("Float deserialization") {
        ConfigOptionFloat opt;
        REQUIRE(opt.deserialize("3.14159"));
        REQUIRE(opt.value == Catch::Approx(3.14159));

        REQUIRE(opt.deserialize("-2.718"));
        REQUIRE(opt.value == Catch::Approx(-2.718));

        REQUIRE_FALSE(opt.deserialize("not_a_number"));
        REQUIRE(opt.value == Catch::Approx(-2.718));  // Value should remain unchanged
    }

    SECTION("Float append operations") {
        ConfigOptionFloat opt;
        opt.value = 3.14;

        // Append should not be supported for floats
        REQUIRE_FALSE(opt.deserialize("1.0", true));
        REQUIRE(opt.value == Catch::Approx(3.14));  // Value should remain unchanged
    }
}

TEST_CASE("ConfigOptionBool operations", "[Config]") {
    SECTION("Basic boolean operations") {
        ConfigOptionBool opt;
        REQUIRE_FALSE(opt.value);

        opt.value = true;
        REQUIRE(opt.value);

        std::string serialized = opt.serialize();
        REQUIRE(serialized == "1");
    }

    SECTION("Boolean deserialization") {
        ConfigOptionBool opt;

        REQUIRE(opt.deserialize("1"));
        REQUIRE(opt.value);

        REQUIRE(opt.deserialize("true"));
        REQUIRE(opt.value);

        REQUIRE(opt.deserialize("yes"));
        REQUIRE(opt.value);

        REQUIRE(opt.deserialize("0"));
        REQUIRE_FALSE(opt.value);

        REQUIRE(opt.deserialize("false"));
        REQUIRE_FALSE(opt.value);

        REQUIRE(opt.deserialize("no"));
        REQUIRE_FALSE(opt.value);

        REQUIRE_FALSE(opt.deserialize("invalid"));
        REQUIRE_FALSE(opt.value);  // Value should remain unchanged
    }

    SECTION("Boolean append operations") {
        ConfigOptionBool opt;
        opt.value = true;

        // Append should not be supported for booleans
        REQUIRE_FALSE(opt.deserialize("1", true));
        REQUIRE(opt.value);  // Value should remain unchanged
    }
}

TEST_CASE("ConfigOptionPoint operations", "[Config]") {
    SECTION("Basic point operations") {
        ConfigOptionPoint opt;
        REQUIRE(opt.value.x() == Catch::Approx(0.0));
        REQUIRE(opt.value.y() == Catch::Approx(0.0));

        opt.value = Vec2d(3.0, 4.0);
        REQUIRE(opt.value.x() == Catch::Approx(3.0));
        REQUIRE(opt.value.y() == Catch::Approx(4.0));

        std::string serialized = opt.serialize();
        REQUIRE(serialized == "3,4");
    }

    SECTION("Point deserialization") {
        ConfigOptionPoint opt;

        REQUIRE(opt.deserialize("3,4"));
        REQUIRE(opt.value.x() == Catch::Approx(3.0));
        REQUIRE(opt.value.y() == Catch::Approx(4.0));

        REQUIRE(opt.deserialize("-2.5,1.5"));
        REQUIRE(opt.value.x() == Catch::Approx(-2.5));
        REQUIRE(opt.value.y() == Catch::Approx(1.5));

        REQUIRE_FALSE(opt.deserialize("invalid"));
        REQUIRE(opt.value.x() == Catch::Approx(-2.5));  // Values should remain unchanged
        REQUIRE(opt.value.y() == Catch::Approx(1.5));
    }
}

TEST_CASE("ConfigOptionPoint3 operations", "[Config]") {
    SECTION("Basic point3 operations") {
        ConfigOptionPoint3 opt;
        REQUIRE(opt.value.x() == Catch::Approx(0.0));
        REQUIRE(opt.value.y() == Catch::Approx(0.0));
        REQUIRE(opt.value.z() == Catch::Approx(0.0));

        opt.value = Vec3d(1.0, 2.0, 3.0);
        REQUIRE(opt.value.x() == Catch::Approx(1.0));
        REQUIRE(opt.value.y() == Catch::Approx(2.0));
        REQUIRE(opt.value.z() == Catch::Approx(3.0));

        std::string serialized = opt.serialize();
        REQUIRE(serialized == "1,2,3");
    }

    SECTION("Point3 deserialization") {
        ConfigOptionPoint3 opt;

        REQUIRE(opt.deserialize("1,2,3"));
        REQUIRE(opt.value.x() == Catch::Approx(1.0));
        REQUIRE(opt.value.y() == Catch::Approx(2.0));
        REQUIRE(opt.value.z() == Catch::Approx(3.0));

        REQUIRE(opt.deserialize("-1.5,2.5,-3.5"));
        REQUIRE(opt.value.x() == Catch::Approx(-1.5));
        REQUIRE(opt.value.y() == Catch::Approx(2.5));
        REQUIRE(opt.value.z() == Catch::Approx(-3.5));

        REQUIRE_FALSE(opt.deserialize("invalid"));
        REQUIRE(opt.value.x() == Catch::Approx(-1.5));  // Values should remain unchanged
        REQUIRE(opt.value.y() == Catch::Approx(2.5));
        REQUIRE(opt.value.z() == Catch::Approx(-3.5));
    }
}

TEST_CASE("DynamicConfig operations", "[Config]") {
    SECTION("Basic dynamic config operations") {
        DynamicConfig config;

        // Add and retrieve a string option
        config.set("string_option", "test_value");
        REQUIRE(config.opt<ConfigOptionString>("string_option")->value == "test_value");

        // Add and retrieve an int option
        config.set("int_option", 42);
        REQUIRE(config.opt<ConfigOptionInt>("int_option")->value == 42);

        // Add and retrieve a float option
        config.set("float_option", 3.14159);
        REQUIRE(config.opt<ConfigOptionFloat>("float_option")->value == Catch::Approx(3.14159));
    }

    SECTION("Option existence checks") {
        DynamicConfig config;
        config.set("existing_option", "value");

        REQUIRE(config.has("existing_option"));
        REQUIRE_FALSE(config.has("non_existing_option"));
    }

    SECTION("Option removal") {
        DynamicConfig config;
        config.set("option_to_remove", "value");

        REQUIRE(config.has("option_to_remove"));
        config.erase("option_to_remove");
        REQUIRE_FALSE(config.has("option_to_remove"));
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

TEST_CASE("Multiple string escaping and unescaping", "[Config]") {
    SECTION("Multiple string escaping") {
        std::vector<std::string> input = {"first", "second", "third"};
        std::string escaped = escape_strings_cstyle(input);
        REQUIRE(escaped == "first;second;third");

        input = {"with space", "with\"quote", "with\\backslash"};
        escaped = escape_strings_cstyle(input);
        REQUIRE(escaped == "\"with space\";\"with\\\"quote\";\"with\\\\backslash\"");
    }

    SECTION("Multiple string unescaping") {
        std::vector<std::string> result;

        REQUIRE(unescape_strings_cstyle("first;second;third", result));
        REQUIRE(result.size() == 3);
        REQUIRE(result[0] == "first");
        REQUIRE(result[1] == "second");
        REQUIRE(result[2] == "third");

        REQUIRE(unescape_strings_cstyle("\"with space\";\"with\\\"quote\";\"with\\\\backslash\"", result));
        REQUIRE(result.size() == 3);
        REQUIRE(result[0] == "with space");
        REQUIRE(result[1] == "with\"quote");
        REQUIRE(result[2] == "with\\backslash");

        // Invalid format
        REQUIRE_FALSE(unescape_strings_cstyle("invalid\"", result));
    }
}

TEST_CASE("Config file operations", "[Config]") {
    SECTION("Save and load JSON config") {
        DynamicConfig config;
        config.set("string_option", "test_value");
        config.set("int_option", 42);
        config.set("float_option", 3.14159);

        // Save to temporary file
        std::string temp_file = "temp_config.json";
        config.save_to_json(temp_file, "test_config", "test", "1.0", "1");

        // Load from file
        DynamicConfig loaded_config;
        std::map<std::string, std::string> key_values;
        std::string reason;
        auto substitutions = loaded_config.load_from_json(temp_file,
            ForwardCompatibilitySubstitutionRule::Enable, key_values, reason);

        // Verify loaded values
        REQUIRE(loaded_config.opt<ConfigOptionString>("string_option")->value == "test_value");
        REQUIRE(loaded_config.opt<ConfigOptionInt>("int_option")->value == 42);
        REQUIRE(loaded_config.opt<ConfigOptionFloat>("float_option")->value == Catch::Approx(3.14159));

        // Cleanup
        std::remove(temp_file.c_str());
    }
}

TEST_CASE("Config substitution context", "[Config]") {
    SECTION("Substitution rules") {
        ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Enable);

        DynamicConfig config;
        config.set("string_option", "old_value");

        // Test substitution
        ConfigOption* new_opt = new ConfigOptionString("new_value");
        ctx.substitutions.push_back(ConfigSubstitution{
            nullptr,  // opt_def
            "old_value",  // old_value
            ConfigOptionUniquePtr(new_opt)  // new_value
        });

        REQUIRE(ctx.substitutions.size() == 1);
        REQUIRE(ctx.substitutions[0].old_value == "old_value");
        REQUIRE(static_cast<ConfigOptionString*>(ctx.substitutions[0].new_value.get())->value == "new_value");
    }
}