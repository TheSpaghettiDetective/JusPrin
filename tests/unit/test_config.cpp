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