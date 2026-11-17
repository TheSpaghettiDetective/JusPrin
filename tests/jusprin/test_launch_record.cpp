// The start report JusPrin sends its server, without the network: the install
// id it keeps, and the body the server reads.

#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>

#include "slic3r/GUI/JusPrin/Updates/LaunchRecord.hpp"

#include "libslic3r/AppConfig.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <optional>
#include <string>

using namespace Slic3r::GUI::JusPrin::Updates;
using Slic3r::AppConfig;
using nlohmann::json;

namespace {

bool lowercase_hex(const std::string& value)
{
    for (unsigned char c : value)
        if (!(std::isdigit(c) || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}

} // namespace

TEST_CASE("An install gets one id and keeps it", "[Updates]")
{
    AppConfig         config;
    const std::string first = install_id(config);
    CHECK(first.size() == kInstallIdLength);
    CHECK(lowercase_hex(first));
    CHECK(config.get(kInstallIdKey) == first);
    CHECK(install_id(config) == first);

    AppConfig other;
    CHECK(install_id(other) != first);
}

TEST_CASE("A stored value that is not an id is replaced", "[Updates]")
{
    AppConfig config;
    config.set(kInstallIdKey, "not-an-id");
    const std::string made = install_id(config);
    CHECK(made.size() == kInstallIdLength);
    CHECK(config.get(kInstallIdKey) == made);
}

TEST_CASE("The start report carries the install id, the app and the system", "[Updates]")
{
    const json body = json::parse(launch_report_body("0123456789abcdef0123456789abcdef", "JusPrin 2.0.0 (OrcaSlicer 2.5.0)", "macOS 26.3"));
    CHECK(body == json{{"install", "0123456789abcdef0123456789abcdef"}, {"app", "JusPrin 2.0.0 (OrcaSlicer 2.5.0)"}, {"os", "macOS 26.3"}});
}

TEST_CASE("A start report the server answered with a fault is reported, one that reached no server is not", "[Updates]")
{
    const std::optional<std::string> fault = launch_failure_report(500, R"({"detail": "server fault"})");
    REQUIRE(fault.has_value());
    CHECK(fault->find("HTTP 500") != std::string::npos);
    CHECK(fault->find(R"({"detail": "server fault"})") != std::string::npos);

    CHECK(launch_failure_report(404, "").has_value());
    // No network, a refused connection or a timeout: nothing answered.
    CHECK_FALSE(launch_failure_report(0, "").has_value());
}
