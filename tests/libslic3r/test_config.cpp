#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <sstream>
#include <string>
#include <vector>
#include <map>

// Minimal implementation for testing
namespace Slic3r {

class ConfigOption {
public:
    virtual ~ConfigOption() {}
    virtual std::string serialize() const = 0;
    virtual bool deserialize(const std::string &str) = 0;
};

class ConfigOptionFloat : public ConfigOption {
public:
    double value;
    ConfigOptionFloat(double v = 0) : value(v) {}

    std::string serialize() const override {
        std::ostringstream ss;
        ss << value;
        return ss.str();
    }

    bool deserialize(const std::string &str) override {
        std::istringstream iss(str);
        iss >> value;
        return !iss.fail();
    }
};

class ConfigOptionInt : public ConfigOption {
public:
    int value;
    ConfigOptionInt(int v = 0) : value(v) {}

    std::string serialize() const override {
        return std::to_string(value);
    }

    bool deserialize(const std::string &str) override {
        try {
            value = std::stoi(str);
            return true;
        } catch (...) {
            return false;
        }
    }
};

class ConfigOptionString : public ConfigOption {
public:
    std::string value;
    ConfigOptionString(const std::string &v = "") : value(v) {}

    std::string serialize() const override {
        return value;
    }

    bool deserialize(const std::string &str) override {
        value = str;
        return true;
    }
};

class ConfigOptionBool : public ConfigOption {
public:
    bool value;
    ConfigOptionBool(bool v = false) : value(v) {}

    std::string serialize() const override {
        return value ? "1" : "0";
    }

    bool deserialize(const std::string &str) override {
        if (str == "1" || str == "true") {
            value = true;
            return true;
        }
        if (str == "0" || str == "false") {
            value = false;
            return true;
        }
        return false;
    }
};

} // namespace Slic3r

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
}