#include "LaunchRecord.hpp"

#include "libslic3r/AppConfig.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <random>
#include <sstream>
#include <string>

namespace Slic3r::GUI::JusPrin::Updates {

namespace {

bool is_install_id(const std::string& value)
{
    return value.size() == kInstallIdLength &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c) || (c >= 'a' && c <= 'f'); });
}

std::string new_install_id()
{
    static std::mt19937_64                       engine(std::random_device{}());
    std::uniform_int_distribution<std::uint64_t> distribution;
    std::ostringstream                           out;
    out << std::hex << std::setfill('0') << std::setw(16) << distribution(engine) << std::setw(16) << distribution(engine);
    return out.str();
}

} // namespace

std::string install_id(AppConfig& config)
{
    const std::string stored = config.get(kInstallIdKey);
    if (is_install_id(stored))
        return stored;
    const std::string made = new_install_id();
    config.set(kInstallIdKey, made);
    return made;
}

std::string launch_report_body(const std::string& install_id, const std::string& app, const std::string& os)
{
    return nlohmann::json{{"install", install_id}, {"app", app}, {"os", os}}.dump();
}

std::optional<std::string> launch_failure_report(unsigned status, const std::string& answer)
{
    if (status == 0)
        return std::nullopt;
    return "JusPrin's server answered the start report with HTTP " + std::to_string(status) + ": " + answer;
}

} // namespace Slic3r::GUI::JusPrin::Updates
