#pragma once

// Every request a transport is given, written to a directory exactly as
// posted, for checking what a provider was sent and cached. Tests and the
// shell harness only. For the n-th request (four digits, in post order):
//   NNNN-request.json  the body, byte for byte
//   NNNN-usage.txt     "<project id> <chat id>" the request was posted in,
//                      then each usage object the response stream carried,
//                      one JSON object per line
// tests/project_prompt/prefix_check.py compares consecutive bodies of a chat.

#include "slic3r/GUI/JusPrin/Agent/AgentTransport.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

namespace Slic3r::GUI::JusPrin::Agent {

class RequestLog
{
public:
    // Chat ids restart in each project, so a chat is named by both.
    struct Chat
    {
        std::string project_id;
        std::string chat_id;
    };

    // `directory` must be new or empty, so a run's numbering never mixes with
    // an earlier run's. `chat` is read at each post.
    RequestLog(std::filesystem::path directory, std::function<Chat()> chat)
        : m_state(std::make_shared<State>(State{std::move(directory), std::move(chat), 0}))
    {
        std::filesystem::create_directories(m_state->directory);
        if (!std::filesystem::is_empty(m_state->directory))
            throw std::runtime_error("the request log directory is not empty: " + m_state->directory.string());
    }

    std::unique_ptr<IAgentHttpTransport> transport(std::unique_ptr<IAgentHttpTransport> inner) const
    {
        return std::make_unique<Transport>(m_state, std::move(inner));
    }

private:
    struct State
    {
        std::filesystem::path directory;
        std::function<Chat()> chat;
        int                   count;
    };

    class Transport final : public IAgentHttpTransport
    {
    public:
        Transport(std::shared_ptr<State> state, std::unique_ptr<IAgentHttpTransport> inner) : m_state(std::move(state)), m_inner(std::move(inner)) {}

        bool post(AgentHttpRequest request, EventFn event) override
        {
            char stem[8];
            std::snprintf(stem, sizeof stem, "%04d", ++m_state->count);
            std::ofstream(m_state->directory / (std::string(stem) + "-request.json"), std::ios::binary) << request.body;
            const Chat chat  = m_state->chat();
            auto       usage = std::make_shared<std::ofstream>(m_state->directory / (std::string(stem) + "-usage.txt"), std::ios::binary);
            *usage << chat.project_id << ' ' << chat.chat_id << '\n' << std::flush;
            auto pending = std::make_shared<std::string>(); // a stream line split across chunks
            return m_inner->post(std::move(request), [usage, pending, event = std::move(event)](AgentHttpEvent received) {
                if (received.kind == AgentHttpEvent::Kind::Data) {
                    *pending += received.data;
                    for (std::size_t end; (end = pending->find('\n')) != std::string::npos; pending->erase(0, end + 1)) {
                        if (pending->compare(0, 5, "data:") != 0)
                            continue;
                        const nlohmann::json line = nlohmann::json::parse(pending->substr(5, end - 5), nullptr, false);
                        // Anthropic: message_start's message.usage and message_delta's usage; Responses: response.usage.
                        for (const char* at : {"/usage", "/message/usage", "/response/usage"}) {
                            const nlohmann::json::json_pointer pointer(at);
                            if (line.is_object() && line.contains(pointer) && line.at(pointer).is_object())
                                *usage << line.at(pointer).dump() << '\n' << std::flush;
                        }
                    }
                }
                event(std::move(received));
            });
        }

        void cancel() override { m_inner->cancel(); }

    private:
        std::shared_ptr<State>               m_state;
        std::unique_ptr<IAgentHttpTransport> m_inner;
    };

    std::shared_ptr<State> m_state;
};

} // namespace Slic3r::GUI::JusPrin::Agent
