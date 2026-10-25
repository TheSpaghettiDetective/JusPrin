#pragma once

// In-memory stand-in for the project document behind the setup tools, so a
// coordinator test sees the behaviour the real store gives it.

#include "slic3r/GUI/JusPrin/Agent/ProductState.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

class FakeProductState final : public IProductState
{
public:
    // Every message belongs to one chat here unless a test names it in
    // chatless_messages, as an external client's call would be.
    std::optional<std::string> setup_title(const std::string& message_id) const override
    {
        if (chatless_messages.count(message_id) != 0)
            return std::nullopt;
        return m_setup_title;
    }

    bool set_setup_title(const std::string& message_id, const std::string& title) override
    {
        // The document's rule: one line of at most 120 characters.
        if (chatless_messages.count(message_id) != 0 || title.empty() || title.size() > 120 ||
            title.find_first_of("\r\n") != std::string::npos)
            return false;
        ++writes;
        m_setup_title = title;
        return true;
    }

    nlohmann::json purpose_setup() const override { return m_purpose_setup; }
    void set_purpose_setup(nlohmann::json setup) override { m_purpose_setup = std::move(setup); }

    PlanRecord plan() const override { return m_plan; }

    PlanRecord set_plan(PlanRecord record) override
    {
        ++writes;
        record.seq        = ++m_seq;
        record.updated_at = timestamp;
        m_plan            = std::move(record);
        return m_plan;
    }

    std::vector<Workspace::RegionRecord> regions() const override { return m_regions; }

    std::vector<Workspace::RegionRecord> set_regions(std::vector<Workspace::RegionRecord> records) override
    {
        ++writes;
        for (Workspace::RegionRecord& record : records)
            if (record.seq == 0) {
                record.seq        = ++m_seq;
                record.updated_at = timestamp;
            }
        m_regions = std::move(records);
        return m_regions;
    }

    // Facts are app-level and time-bound in the real store; here they are
    // just remembered, which is all a coordinator test needs to observe.
    void flush_to_project() override { ++flushes; }
    bool has_printer_facts() const override { return true; }

    std::vector<Workspace::PrinterFact> printer_facts(const std::string& printer) const override
    {
        std::vector<Workspace::PrinterFact> result;
        for (const auto& fact : m_facts)
            if (fact.printer == printer) result.push_back(fact);
        return result;
    }

    std::vector<Workspace::PrinterFact> confirm_printer_facts(
        const std::string& printer, const std::vector<Workspace::FactConfirmation>& confirmations) override
    {
        ++writes;
        for (const auto& confirmation : confirmations) {
            Workspace::PrinterFact stated{printer, confirmation.fact, confirmation.value, timestamp, "2026-09-17T00:00:00Z"};
            const auto existing = std::find_if(m_facts.begin(), m_facts.end(), [&](const Workspace::PrinterFact& fact) {
                return fact.printer == printer && fact.fact == confirmation.fact;
            });
            if (existing != m_facts.end()) *existing = stated; else m_facts.push_back(stated);
        }
        return printer_facts(printer);
    }

    std::string   timestamp{"2026-09-16T00:00:00Z"};
    std::uint32_t writes{0};
    std::uint32_t flushes{0};
    std::set<std::string> chatless_messages;

private:
    std::string              m_setup_title;
    nlohmann::json           m_purpose_setup = nlohmann::json::object();
    PlanRecord               m_plan;
    std::vector<Workspace::RegionRecord> m_regions;
    std::vector<Workspace::PrinterFact> m_facts;
    std::uint64_t            m_seq{0};
};

} // namespace Slic3r::GUI::JusPrin::Agent
