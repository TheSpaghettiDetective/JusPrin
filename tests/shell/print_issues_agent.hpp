#pragma once

// An Agent service that records every reply request it is given, so the
// print-issues scenario can read what a press of "Ask AI" or "Resolve with
// AI" actually sent, and that can be held mid-reply to exercise the busy path.

#include "slic3r/GUI/JusPrin/Agent/AgentService.hpp"

#include <deque>
#include <vector>

namespace Slic3r::GUI::JusPrin {

class RecordingAgent final : public Agent::IAgentService
{
public:
    bool ready() const override { return true; }
    bool busy() const override { return m_active; }
    bool start(const Agent::AgentRequest& request) override
    {
        m_active = true;
        if (request.purpose != Agent::AgentRequest::Purpose::ConversationTitle)
            requests.push_back(request);
        if (!hold)
            reply();
        return true;
    }
    bool continue_after_tool(const Agent::AgentToolResult&) override
    {
        reply();
        return true;
    }
    void cancel() override
    {
        m_active = false;
        m_events.clear();
    }
    std::optional<Agent::AgentEvent> poll() override
    {
        if (m_events.empty())
            return std::nullopt;
        Agent::AgentEvent event = std::move(m_events.front());
        m_events.pop_front();
        if (event.kind == Agent::AgentEventKind::Completed || event.kind == Agent::AgentEventKind::Failed)
            m_active = false;
        return event;
    }
    // Lets a held reply finish.
    void release()
    {
        hold = false;
        if (m_active && m_events.empty())
            reply();
    }

    std::vector<Agent::AgentRequest> requests;
    bool                             hold{false};

private:
    void reply()
    {
        m_events.push_back(Agent::AgentEvent::delta("Noted."));
        m_events.push_back(Agent::AgentEvent::completed());
    }

    std::deque<Agent::AgentEvent> m_events;
    bool                          m_active{false};
};

} // namespace Slic3r::GUI::JusPrin
