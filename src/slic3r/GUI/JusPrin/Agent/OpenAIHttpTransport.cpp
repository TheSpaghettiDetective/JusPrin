#include "OpenAIResponsesAgent.hpp"
#include "slic3r/GUI/JusPrin/Support/ErrorReport.hpp"
#include "AgentTransport.hpp"

#include "slic3r/Utils/Http.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/trim.hpp>

#include <cstddef>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

// The service's id for the request, from the response's headers as they
// have come so far: Anthropic's request-id, OpenAI's x-request-id.
std::string request_id_in(const std::string& headers)
{
    std::string        found;
    std::istringstream lines(headers);
    std::string        line;
    while (std::getline(lines, line)) {
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        const std::string name = boost::algorithm::to_lower_copy(line.substr(0, colon));
        if (name == "request-id" || name == "x-request-id")
            found = boost::algorithm::trim_copy(line.substr(colon + 1));
    }
    return found;
}

class OpenAIHttpTransport final : public IAgentHttpTransport
{
public:
    bool post(AgentHttpRequest request, EventFn event) override
    {
        cancel();
        auto state = std::make_shared<State>();
        state->event = std::move(event);
        m_state = state;

        auto http = Slic3r::Http::post(std::move(request.url));
        // The credential and the codec's own headers, then what every
        // JSON-over-SSE provider is sent.
        for (const AgentHttpHeader& header : request.headers)
            http.header(header.name, header.value);
        http.header("Content-Type", "application/json")
            .header("Accept", "text/event-stream")
            .tls_verify(true)
            .timeout_connect(request.connect_timeout_seconds)
            .timeout_max(request.request_timeout_seconds)
            .size_limit(request.response_size_limit)
            .set_post_body(request.body)
            .on_header_callback(reported("receiving the AI's reply", [weak = std::weak_ptr<State>(state)](std::string headers) {
                const auto locked = weak.lock();
                if (!locked)
                    return;
                std::lock_guard<std::mutex> guard(locked->mutex);
                locked->request_id = request_id_in(headers);
            }))
            .on_progress(reported("receiving the AI's reply", [weak = std::weak_ptr<State>(state)](Slic3r::Http::Progress progress, bool& should_cancel) {
                const auto locked = weak.lock();
                if (!locked)
                    return;
                std::lock_guard<std::mutex> guard(locked->mutex);
                should_cancel = locked->cancelled;
                if (locked->cancelled || progress.buffer.size() <= locked->seen)
                    return;
                std::string delta = progress.buffer.substr(locked->seen);
                locked->seen = progress.buffer.size();
                locked->event(AgentHttpEvent{AgentHttpEvent::Kind::Data, std::move(delta), {}, 0, locked->request_id});
            }))
            .on_complete(reported("receiving the AI's reply", [weak = std::weak_ptr<State>(state)](std::string body, unsigned status) {
                const auto locked = weak.lock();
                if (!locked)
                    return;
                std::lock_guard<std::mutex> guard(locked->mutex);
                if (locked->cancelled)
                    return;
                if (body.size() > locked->seen)
                    locked->event(AgentHttpEvent{AgentHttpEvent::Kind::Data, body.substr(locked->seen), {}, status, locked->request_id});
                locked->event(AgentHttpEvent{AgentHttpEvent::Kind::Complete, {}, {}, status, locked->request_id});
            }))
            .on_error(reported("receiving the AI's reply", [weak = std::weak_ptr<State>(state)](std::string body, std::string error, unsigned status) {
                const auto locked = weak.lock();
                if (!locked)
                    return;
                std::lock_guard<std::mutex> guard(locked->mutex);
                if (!locked->cancelled)
                    locked->event(AgentHttpEvent{AgentHttpEvent::Kind::Error, std::move(body), std::move(error), status, locked->request_id});
            }));
        state->http = http.perform();
        return state->http != nullptr;
    }

    void cancel() override
    {
        const auto state = std::move(m_state);
        if (!state)
            return;
        Slic3r::Http::Ptr http;
        {
            std::lock_guard<std::mutex> guard(state->mutex);
            state->cancelled = true;
            http = state->http;
        }
        if (http)
            http->cancel();
    }

private:
    struct State
    {
        std::mutex       mutex;
        EventFn          event;
        Slic3r::Http::Ptr http;
        std::size_t      seen{0};
        std::string      request_id;
        bool             cancelled{false};
    };
    std::shared_ptr<State> m_state;
};

} // namespace

std::unique_ptr<IAgentHttpTransport> make_openai_http_transport()
{
    return std::make_unique<OpenAIHttpTransport>();
}

} // namespace Slic3r::GUI::JusPrin::Agent
