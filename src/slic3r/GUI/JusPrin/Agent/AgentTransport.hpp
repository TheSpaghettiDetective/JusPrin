#pragma once

// One streaming HTTP POST to a model provider, and the events it yields.
// Provider-neutral: the request carries whatever headers the provider's codec
// and the credential call for, and the transport adds only what every
// JSON-over-SSE provider needs.

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

struct AgentHttpHeader
{
    std::string name;
    std::string value;
};

struct AgentHttpRequest
{
    std::string                  url;
    std::vector<AgentHttpHeader> headers;
    std::string                  body;
    long                         connect_timeout_seconds{10};
    long                         request_timeout_seconds{120};
    std::size_t                  response_size_limit{16u * 1024u * 1024u};

    // The value of the first header of that name, or empty.
    std::string header(const std::string& header_name) const
    {
        for (const AgentHttpHeader& header : headers)
            if (header.name == header_name)
                return header.value;
        return {};
    }
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

} // namespace Slic3r::GUI::JusPrin::Agent
