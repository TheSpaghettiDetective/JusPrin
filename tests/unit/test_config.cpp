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