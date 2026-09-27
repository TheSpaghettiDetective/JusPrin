#include "HostLanes.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin::Printers {

using nlohmann::json;

namespace {

// The field when it is a string, as upstream's safe_json_string reads it.
std::string string_field(const json& object, const char* key)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
}

std::string string_at(const json& list, std::size_t index)
{
    return index < list.size() && list[index].is_string() ? list[index].get<std::string>() : std::string();
}

int int_at(const json& list, std::size_t index)
{
    return index < list.size() && list[index].is_number() ? list[index].get<int>() : 0;
}

std::optional<json> parse(const std::string& body)
{
    json parsed = json::parse(body, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    if (parsed.is_discarded() || !parsed.is_object())
        return std::nullopt;
    return parsed;
}

} // namespace

std::string lane_colour(const std::string& raw)
{
    std::string value = raw;
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c) { return !std::isspace(c); }));
    if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0)
        value = value.substr(2);
    if (!value.empty() && value.front() == '#')
        value = value.substr(1);
    // Upstream keeps the hex digits and drops the rest, so "#None" is not a
    // colour; neither is anything that does not come to six or eight digits.
    std::string digits;
    for (const unsigned char c : value)
        if (std::isxdigit(c))
            digits.push_back(static_cast<char>(std::toupper(c)));
    if (digits.size() != 6 && digits.size() != 8)
        return {};
    return "#" + digits.substr(0, 6);
}

std::optional<UnitReport> parse_lane_data(const std::string& body)
{
    const auto reply = parse(body);
    if (!reply)
        return std::nullopt;
    const auto result = reply->find("result");
    if (result == reply->end() || !result->is_object())
        return std::nullopt;
    const auto value = result->find("value");
    if (value == result->end() || !value->is_object())
        return std::nullopt;

    UnitReport report;
    for (const auto& [key, lane] : value->items()) {
        if (!lane.is_object())
            continue;
        const std::string number = string_field(lane, "lane");
        int               index  = -1;
        try {
            index = number.empty() ? -1 : std::stoi(number);
        } catch (const std::exception&) {
            index = -1;
        }
        if (index < 0)
            continue;
        report.unit = true;
        const std::string material = string_field(lane, "material");
        if (!material.empty())
            report.loaded.push_back(HostLane{index, material, lane_colour(string_field(lane, "color"))});
    }
    std::sort(report.loaded.begin(), report.loaded.end(),
              [](const HostLane& a, const HostLane& b) { return a.index < b.index; });
    return report;
}

std::optional<UnitReport> parse_happy_hare(const std::string& body)
{
    const auto reply = parse(body);
    if (!reply)
        return std::nullopt;
    const auto result = reply->find("result");
    if (result == reply->end() || !result->is_object())
        return std::nullopt;
    const auto status = result->find("status");
    if (status == result->end() || !status->is_object())
        return std::nullopt;

    UnitReport report;
    const auto mmu = status->find("mmu");
    // Klipper leaves out an object it does not have, and Happy Hare not
    // installed leaves an empty one.
    if (mmu == status->end() || !mmu->is_object() || mmu->empty())
        return report;
    const auto gates = mmu->find("num_gates");
    if (gates == mmu->end() || !gates->is_number() || gates->get<int>() <= 0)
        return report;
    const auto list = [&mmu](const char* key) {
        const auto found = mmu->find(key);
        return found == mmu->end() ? json::array() : *found;
    };
    const json gate_status = list("gate_status"), gate_material = list("gate_material"), gate_color = list("gate_color"),
               gate_temperature = list("gate_temperature");
    if (!gate_status.is_array() || !gate_material.is_array() || !gate_color.is_array() || !gate_temperature.is_array())
        return report;

    report.unit = true;
    const int count = gates->get<int>();
    for (int gate = 0; gate < count; ++gate) {
        // -1 unknown, 0 empty, 1 or 2 available.
        if (int_at(gate_status, gate) <= 0)
            continue;
        const std::string material = string_at(gate_material, gate);
        if (!material.empty())
            report.loaded.push_back(HostLane{gate, material, lane_colour(string_at(gate_color, gate))});
    }
    return report;
}

HostStatus HostReadings::status(const std::string& key, Clock::time_point now) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_entries.find(key);
    if (found == m_entries.end())
        return {};
    const Entry& entry    = found->second;
    const auto   is_fresh = [now](const std::optional<Clock::time_point>& at) { return at && now - *at < kHostReadingFresh; };
    HostStatus   status;
    if (is_fresh(entry.answered)) {
        status.link   = HostLink::Reachable;
        status.loaded = entry.loaded;
    } else if (is_fresh(entry.failed) && (!entry.answered || *entry.failed > *entry.answered)) {
        status.link = HostLink::Unreachable;
    }
    return status;
}

bool HostReadings::begin(const std::string& key, Clock::time_point now)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Entry& entry = m_entries[key];
    if (entry.in_flight || (entry.started && now - *entry.started < kHostPollInterval))
        return false;
    entry.in_flight = true;
    entry.started   = now;
    return true;
}

void HostReadings::finish(const std::string& key, HostReading reading, Clock::time_point now)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Entry& entry    = m_entries[key];
    entry.in_flight = false;
    if (reading.reached) {
        entry.answered = now;
        entry.loaded   = std::move(reading.loaded);
    } else {
        entry.failed = now;
    }
}

void HostReadings::note_answered(const std::string& key, Clock::time_point now)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries[key].answered = now;
}

} // namespace Slic3r::GUI::JusPrin::Printers
