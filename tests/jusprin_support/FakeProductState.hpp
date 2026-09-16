#pragma once

// In-memory stand-in for the project document behind the intent and plan
// tools. It applies the same upsert-by-field-name rule the document does, so a
// coordinator test sees the behaviour the real store gives it.

#include "slic3r/GUI/JusPrin/Agent/ProductState.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

class FakeProductState final : public IProductState
{
public:
    std::vector<IntentField> print_intent() const override { return m_fields; }

    std::vector<IntentField> set_print_intent(const std::vector<IntentField>& fields) override
    {
        ++writes;
        for (const IntentField& field : fields) {
            IntentField written = field;
            written.seq         = ++m_seq;
            written.updated_at  = timestamp;
            const auto existing = std::find_if(m_fields.begin(), m_fields.end(),
                                               [&field](const IntentField& stored) { return stored.field == field.field; });
            if (existing != m_fields.end())
                *existing = written;
            else
                m_fields.insert(std::upper_bound(m_fields.begin(), m_fields.end(), written,
                                                 [](const IntentField& lhs, const IntentField& rhs) {
                                                     return lhs.field < rhs.field;
                                                 }),
                                written);
        }
        return m_fields;
    }

    PlanRecord plan() const override { return m_plan; }

    PlanRecord set_plan(PlanRecord record) override
    {
        ++writes;
        record.seq        = ++m_seq;
        record.updated_at = timestamp;
        m_plan            = std::move(record);
        return m_plan;
    }

    std::string   timestamp{"2026-09-16T00:00:00Z"};
    std::uint32_t writes{0};

private:
    std::vector<IntentField> m_fields;
    PlanRecord               m_plan;
    std::uint64_t            m_seq{0};
};

} // namespace Slic3r::GUI::JusPrin::Agent
