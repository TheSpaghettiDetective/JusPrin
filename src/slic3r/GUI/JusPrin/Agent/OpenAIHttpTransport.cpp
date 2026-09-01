#include "OpenAIResponsesAgent.hpp"

#include "slic3r/Utils/Http.hpp"

#include <memory>
#include <mutex>
#include <utility>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

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
        http.header("Authorization", request.authorization)
            .header("Idempotency-Key", request.idempotency_key)
            .header("Content-Type", "application/json")
            .header("Accept", "text/event-stream")
            .tls_verify(true)
            .timeout_connect(request.connect_timeout_seconds)
            .timeout_max(request.request_timeout_seconds)
            .size_limit(request.response_size_limit)
            .set_post_body(request.body)
            .on_progress([weak = std::weak_ptr<State>(state)](Slic3r::Http::Progress progress, bool& should_cancel) {
                const auto locked = weak.lock();
                if (!locked)
                    return;
                std::lock_guard<std::mutex> guard(locked->mutex);
                should_cancel = locked->cancelled;
                if (locked->cancelled || progress.buffer.size() <= locked->seen)
                    return;
                std::string delta = progress.buffer.substr(locked->seen);
                locked->seen = progress.buffer.size();
                locked->event(AgentHttpEvent{AgentHttpEvent::Kind::Data, std::move(delta), {}, 0});
            })
            .on_complete([weak = std::weak_ptr<State>(state)](std::string body, unsigned status) {
                const auto locked = weak.lock();
                if (!locked)
                    return;
                std::lock_guard<std::mutex> guard(locked->mutex);
                if (locked->cancelled)
                    return;
                if (body.size() > locked->seen)
                    locked->event(AgentHttpEvent{AgentHttpEvent::Kind::Data, body.substr(locked->seen), {}, status});
                locked->event(AgentHttpEvent{AgentHttpEvent::Kind::Complete, {}, {}, status});
            })
            .on_error([weak = std::weak_ptr<State>(state)](std::string, std::string error, unsigned status) {
                const auto locked = weak.lock();
                if (!locked)
                    return;
                std::lock_guard<std::mutex> guard(locked->mutex);
                if (!locked->cancelled)
                    locked->event(AgentHttpEvent{AgentHttpEvent::Kind::Error, {}, std::move(error), status});
            });
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
