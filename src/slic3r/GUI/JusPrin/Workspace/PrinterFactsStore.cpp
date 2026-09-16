#include "PrinterFactsStore.hpp"
#include "UtcTime.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin::Workspace {

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

constexpr int kSchemaVersion = 1;

PrinterFact fact_from(const json& entry)
{
    return {entry.at("printer").get<std::string>(), entry.at("fact").get<std::string>(),
            entry.at("value").get<std::string>(), entry.at("confirmed_at").get<std::string>(),
            entry.at("expires_at").get<std::string>()};
}

json json_from(const PrinterFact& fact)
{
    return json{{"printer", fact.printer},
                {"fact", fact.fact},
                {"value", fact.value},
                {"confirmed_at", fact.confirmed_at},
                {"expires_at", fact.expires_at}};
}

} // namespace

PrinterFactsStore::PrinterFactsStore(Config config) : m_config(std::move(config))
{
    if (m_config.file_path.empty())
        throw std::invalid_argument("PrinterFactsStore requires a file path");
    if (!m_config.now)
        m_config.now = [] { return std::chrono::system_clock::now(); };

    const fs::path  path(m_config.file_path);
    std::error_code ec;
    if (!fs::exists(path, ec))
        return;

    std::string text;
    {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
            m_corrupt        = true;
            m_corrupt_reason = "printer facts file could not be opened";
            return;
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        text = buffer.str();
    }
    try {
        const json document = json::parse(text);
        for (const auto& entry : document.at("facts"))
            m_facts.push_back(fact_from(entry));
    } catch (const json::exception& error) {
        m_facts.clear();
        m_corrupt        = true;
        m_corrupt_reason = error.what();
    }
    if (m_corrupt) {
        fs::rename(path, path.string() + ".corrupt", ec);
        if (ec)
            m_corrupt_reason += "; the damaged file could not be moved aside: " + ec.message();
    }
}

std::vector<PrinterFact> PrinterFactsStore::current(const std::string& printer) const
{
    // ISO 8601 UTC at a fixed width orders the same as the moments it names,
    // so expiry is a string comparison against the same spelling of now.
    const std::string        now = utc_timestamp(m_config.now());
    std::vector<PrinterFact> result;
    for (const PrinterFact& fact : m_facts)
        if (fact.printer == printer && fact.expires_at > now)
            result.push_back(fact);
    std::sort(result.begin(), result.end(),
              [](const PrinterFact& a, const PrinterFact& b) { return a.fact < b.fact; });
    return result;
}

std::vector<PrinterFact> PrinterFactsStore::confirm(const std::string&                   printer,
                                                    const std::vector<FactConfirmation>& confirmations)
{
    const auto        now_point = m_config.now();
    const std::string now       = utc_timestamp(now_point);
    for (const FactConfirmation& confirmation : confirmations) {
        PrinterFact stated{printer, confirmation.fact, confirmation.value, now,
                           utc_timestamp(now_point + confirmation.lifetime)};
        const auto existing = std::find_if(m_facts.begin(), m_facts.end(), [&](const PrinterFact& fact) {
            return fact.printer == printer && fact.fact == confirmation.fact;
        });
        if (existing != m_facts.end())
            *existing = std::move(stated);
        else
            m_facts.push_back(std::move(stated));
    }
    // An expired fact is not true any more; keeping it on disk would only let
    // a later reader mistake it for something the person said recently.
    m_facts.erase(std::remove_if(m_facts.begin(), m_facts.end(),
                                 [&now](const PrinterFact& fact) { return fact.expires_at <= now; }),
                  m_facts.end());
    write();
    return current(printer);
}

void PrinterFactsStore::write()
{
    json document;
    document["version"] = kSchemaVersion;
    document["facts"]   = json::array();
    for (const PrinterFact& fact : m_facts)
        document["facts"].push_back(json_from(fact));

    const fs::path  path(m_config.file_path);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const fs::path temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            throw std::runtime_error("could not open " + temp.string() + " to save printer facts");
        out << document.dump(2);
        if (!out.good())
            throw std::runtime_error("could not write " + temp.string());
    }
    fs::rename(temp, path, ec);
    if (ec)
        throw std::runtime_error("could not save printer facts to " + path.string() + ": " + ec.message());
}

} // namespace Slic3r::GUI::JusPrin::Workspace
