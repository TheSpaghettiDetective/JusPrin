#pragma once

// The provider-neutral turn loop behind IAgentService. It owns the
// transcript of the turn, runs the model's tool calls one at a time in the
// order written, answers the rest unrun after a failure, refuses calls to
// tools not offered or with arguments off the contract, and sends fired
// skills on one request only. Everything on the wire goes through the
// IProviderCodec it is built with, and nothing here reads provider JSON.
//
// Network callbacks only append protected input; all parsing and AgentEvent
// delivery happens from poll() on the GUI thread.

#include "AgentService.hpp"
#include "AgentTransport.hpp"
#include "ProviderCodec.hpp"
#include "ProviderProfile.hpp"

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

struct ProviderAgentConfig
{
    // The provider, with the configured model and endpoint in place of the
    // row's defaults where the person set them.
    ProviderProfile profile;
    std::string     api_key;
    std::function<void(const AgentUsage&)> usage_listener;
    // A tool call the loop refused before it reached a card: the tool name,
    // the arguments as the model sent them, and why.
    std::function<void(const std::string&, const std::string&, const std::string&)> refusal_listener;
};

class ProviderAgent : public IAgentService
{
public:
    ProviderAgent(ProviderAgentConfig config, std::unique_ptr<IProviderCodec> codec, std::unique_ptr<IAgentHttpTransport> transport);
    ~ProviderAgent() override;

    bool ready() const override;
    bool busy() const override;
    bool start(const AgentRequest& request) override;
    bool continue_after_tool(const AgentToolResult& result) override;
    void cancel() override;
    std::optional<AgentEvent> poll() override;

private:
    struct QueuedHttpEvent
    {
        std::uint64_t generation{0};
        AgentHttpEvent event;
    };

    void begin_transcript(const AgentRequest& request);
    bool post();
    void accept_http(std::uint64_t generation, AgentHttpEvent event);
    void parse_available();
    void parse_sse_frame(const std::string& frame);
    void handle(StreamEvent event);
    void finish_response(StreamEvent event);
    bool next_call();
    void answer_call(const std::string& call_id, const nlohmann::json& output);
    void fail(AgentError error);
    AgentError http_error(unsigned status, const std::string& detail) const;

    ProviderAgentConfig                   m_config;
    std::unique_ptr<IProviderCodec>       m_codec;
    std::unique_ptr<IAgentHttpTransport>  m_transport;
    mutable std::mutex                    m_mutex;
    std::deque<QueuedHttpEvent>           m_http_events;
    std::deque<AgentEvent>                m_events;
    std::string                           m_sse_buffer;
    // What the model has been told this turn; every request is built from it.
    TurnTranscript                        m_transcript;
    RequestKind                           m_kind{RequestKind::Reply};
    std::string                           m_pending_call_id;
    std::string                           m_pending_tool;
    // A response may hold several calls. They run one at a time, in the
    // order the model wrote them, and every one gets a result; the results
    // go back together in one request once the last has its answer.
    std::deque<ProviderToolCall>          m_queued_calls;
    // The skills the response's results fired, sent after all of them on the
    // next request only; one per name, the latest kept.
    std::vector<AgentSkillUse>            m_step_skills;
    // The first call of this response that did not succeed, as "tool
    // (call_id)"; every call after it is answered without being run.
    std::string                           m_failed_call;
    unsigned                              m_rejected_calls{0};
    unsigned                              m_paused_turns{0};
    std::uint64_t                         m_http_generation{0};
    bool                                  m_busy{false};
    bool                                  m_waiting_for_tool{false};
    bool                                  m_terminal_seen{false};
};

} // namespace Slic3r::GUI::JusPrin::Agent
