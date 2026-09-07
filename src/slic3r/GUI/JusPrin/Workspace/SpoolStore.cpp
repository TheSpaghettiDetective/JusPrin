#include "SpoolStore.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin::Workspace {

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

constexpr int kSchemaVersion = 1;

std::string default_clock()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

std::string default_uuid()
{
    static std::mt19937_64 engine(std::random_device{}());
    std::uniform_int_distribution<std::uint64_t> distribution;
    std::ostringstream out;
    out << std::hex << distribution(engine) << '-' << distribution(engine);
    return out.str();
}

Spool spool_from(const json& entry)
{
    Spool spool;
    spool.id              = entry.at("id").get<std::string>();
    spool.printer_preset  = entry.at("printer_preset").get<std::string>();
    spool.filament_preset = entry.at("filament_preset").get<std::string>();
    spool.colour          = entry.at("colour").get<std::string>();
    spool.name            = entry.at("name").get<std::string>();
    spool.brand           = entry.value("brand", std::string{});
    spool.last_used       = entry.value("last_used", std::string{});
    return spool;
}

json json_from(const Spool& spool)
{
    return json{{"id", spool.id},
                {"printer_preset", spool.printer_preset},
                {"filament_preset", spool.filament_preset},
                {"colour", spool.colour},
                {"name", spool.name},
                {"brand", spool.brand},
                {"last_used", spool.last_used}};
}

} // namespace

SpoolStore::SpoolStore(Config config) : m_config(std::move(config))
{
    if (m_config.file_path.empty())
        throw std::invalid_argument("SpoolStore requires a file path");
    if (!m_config.clock)
        m_config.clock = default_clock;
    if (!m_config.uuid)
        m_config.uuid = default_uuid;

    const fs::path path(m_config.file_path);
    std::error_code ec;
    if (!fs::exists(path, ec))
        return;

    std::string text;
    {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
            m_corrupt        = true;
            m_corrupt_reason = "spools file could not be opened";
            return;
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        text = buffer.str();
    }

    // A damaged file is data the person cannot get back. Parse defensively
    // here -- and only here -- then move the file aside so the next write
    // cannot overwrite it. Everything past this point treats the in-memory
    // vector as authoritative.
    try {
        const json document = json::parse(text);
        for (const auto& entry : document.at("spools"))
            m_spools.push_back(spool_from(entry));
    } catch (const json::exception& error) {
        m_spools.clear();
        m_corrupt        = true;
        m_corrupt_reason = error.what();
    }

    if (m_corrupt) {
        fs::rename(path, path.string() + ".corrupt", ec);
        if (ec)
            m_corrupt_reason += "; the damaged file could not be moved aside: " + ec.message();
    }
}

std::vector<Spool> SpoolStore::spools_for(const std::string& printer_preset) const
{
    std::vector<Spool> matches;
    for (const Spool& spool : m_spools)
        if (spool.printer_preset == printer_preset)
            matches.push_back(spool);
    std::sort(matches.begin(), matches.end(), [](const Spool& a, const Spool& b) {
        if (a.last_used != b.last_used)
            return a.last_used > b.last_used;
        return a.id < b.id;
    });
    return matches;
}

std::optional<Spool> SpoolStore::current(const std::string& printer_preset,
                                         const std::string& filament_preset,
                                         const std::string& colour) const
{
    // Most recently used wins when two spools of the same preset share a
    // colour, which is the same order the menu shows.
    for (const Spool& spool : spools_for(printer_preset))
        if (spool.filament_preset == filament_preset && spool.colour == colour)
            return spool;
    return std::nullopt;
}

std::optional<Spool> SpoolStore::find(const std::string& id) const
{
    const auto found = std::find_if(m_spools.begin(), m_spools.end(), [&](const Spool& s) { return s.id == id; });
    return found == m_spools.end() ? std::nullopt : std::optional<Spool>(*found);
}

Spool SpoolStore::add(Spool spool)
{
    spool.id        = m_config.uuid();
    spool.last_used = m_config.clock();
    m_spools.push_back(spool);
    write();
    return spool;
}

bool SpoolStore::rename(const std::string& id, const std::string& name)
{
    auto found = std::find_if(m_spools.begin(), m_spools.end(), [&](const Spool& s) { return s.id == id; });
    if (found == m_spools.end())
        return false;
    found->name = name;
    write();
    return true;
}

bool SpoolStore::recolour(const std::string& id, const std::string& colour)
{
    auto found = std::find_if(m_spools.begin(), m_spools.end(), [&](const Spool& s) { return s.id == id; });
    if (found == m_spools.end())
        return false;
    found->colour = colour;
    write();
    return true;
}

bool SpoolStore::remove(const std::string& id)
{
    auto found = std::find_if(m_spools.begin(), m_spools.end(), [&](const Spool& s) { return s.id == id; });
    if (found == m_spools.end())
        return false;
    m_spools.erase(found);
    write();
    return true;
}

bool SpoolStore::touch(const std::string& id)
{
    auto found = std::find_if(m_spools.begin(), m_spools.end(), [&](const Spool& s) { return s.id == id; });
    if (found == m_spools.end())
        return false;
    found->last_used = m_config.clock();
    write();
    return true;
}

void SpoolStore::write()
{
    json document;
    document["version"] = kSchemaVersion;
    document["spools"]  = json::array();
    for (const Spool& spool : m_spools)
        document["spools"].push_back(json_from(spool));

    const fs::path path(m_config.file_path);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    const fs::path temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            throw std::runtime_error("could not open " + temp.string() + " to save spools");
        out << document.dump(2);
        if (!out.good())
            throw std::runtime_error("could not write " + temp.string());
    }
    fs::rename(temp, path, ec);
    if (ec)
        throw std::runtime_error("could not save spools to " + path.string() + ": " + ec.message());
}

} // namespace Slic3r::GUI::JusPrin::Workspace
