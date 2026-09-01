#pragma once

#include "AgentService.hpp"

#include <nlohmann/json.hpp>

#include <deque>
#include <functional>
#include <memory>
#include <mutex>

namespace Slic3r::GUI::JusPrin::Agent {

struct AgentHttpRequest
{
    std::string url;
    std::string authorization;
    std::string idempotency_key;
    std::string body;
    long        connect_timeout_seconds{10};
    long        request_timeout_seconds{120};
    std::size_t response_size_limit{16u * 1024u * 1024u};
};

struct AgentHttpEvent
{
    enum class Kind { Data, Complete, Error } kind{Kind::Data};
    std::string data;
    std::string error;
    unsigned    status{0};
};

class IAgentHttpTransport
{
public:
    using EventFn = std::function<void(AgentHttpEvent)>;
    virtual ~IAgentHttpTransport() = default;
    virtual bool post(AgentHttpRequest request, EventFn event) = 0;
    virtual void cancel() = 0;
};

struct OpenAIResponsesConfig
{
    std::string api_key;
    std::string model{"gpt-5.4-mini"};
    std::string endpoint{"https://api.openai.com/v1/responses"};
    std::function<void(std::uint64_t input_tokens, std::uint64_t output_tokens, std::uint64_t total_tokens)>
        usage_listener;
};

// Responses API adapter. Network callbacks only append protected input; all
// parsing and AgentEvent delivery happens from poll() on the GUI thread.
class OpenAIResponsesAgent final : public IAgentService
{
public:
    OpenAIResponsesAgent(OpenAIResponsesConfig config, std::unique_ptr<IAgentHttpTransport> transport);
    ~OpenAIResponsesAgent() override;

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

    bool post(nlohmann::json input);
    nlohmann::json request_body(nlohmann::json input) const;
    nlohmann::json initial_input(const AgentRequest& request) const;
    void accept_http(std::uint64_t generation, AgentHttpEvent event);
    void parse_available();
    void parse_sse_frame(const std::string& frame);
    void finish_response(const nlohmann::json& response);
    void fail(AgentError error);
    AgentError http_error(unsigned status, const std::string& detail) const;

    OpenAIResponsesConfig                 m_config;
    std::unique_ptr<IAgentHttpTransport>  m_transport;
    mutable std::mutex                    m_mutex;
    std::deque<QueuedHttpEvent>           m_http_events;
    std::deque<AgentEvent>                m_events;
    std::string                           m_sse_buffer;
    nlohmann::json                        m_prior_output{nlohmann::json::array()};
    std::string                           m_pending_call_id;
    std::string                           m_request_id;
    unsigned                              m_request_sequence{0};
    std::uint64_t                         m_http_generation{0};
    bool                                  m_busy{false};
    bool                                  m_waiting_for_tool{false};
    bool                                  m_terminal_seen{false};
    bool                                  m_allow_import{false};
};

std::unique_ptr<IAgentHttpTransport> make_openai_http_transport();

} // namespace Slic3r::GUI::JusPrin::Agent
