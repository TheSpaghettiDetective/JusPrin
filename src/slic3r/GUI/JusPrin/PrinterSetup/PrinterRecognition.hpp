#pragma once

#include "PrinterSetupTypes.hpp"
#include "slic3r/GUI/JusPrin/Agent/OpenAIResponsesAgent.hpp"

#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

class IPrinterRecognitionService
{
public:
    virtual ~IPrinterRecognitionService() = default;
    virtual bool ready() const = 0;
    virtual bool start(std::uint64_t generation, const PrinterEvidence& evidence,
                       const std::vector<const PrinterCandidate*>& choices) = 0;
    virtual void cancel() = 0;
    virtual std::optional<RecognitionEvent> poll() = 0;
};

struct OpenAIPrinterRecognitionConfig
{
    std::string api_key;
    std::string model{"gpt-5.4-mini"};
    std::string endpoint{"https://api.openai.com/v1/responses"};
};

class OpenAIPrinterRecognition final : public IPrinterRecognitionService
{
public:
    OpenAIPrinterRecognition(OpenAIPrinterRecognitionConfig config,
                             std::unique_ptr<Agent::IAgentHttpTransport> transport);
    ~OpenAIPrinterRecognition() override;

    bool ready() const override;
    bool start(std::uint64_t generation, const PrinterEvidence& evidence,
               const std::vector<const PrinterCandidate*>& choices) override;
    void cancel() override;
    std::optional<RecognitionEvent> poll() override;

private:
    struct HttpItem { std::uint64_t generation; Agent::AgentHttpEvent event; };
    void accept(std::uint64_t generation, Agent::AgentHttpEvent event);
    RecognitionEvent parse(std::uint64_t generation, unsigned status, const std::string& body) const;

    OpenAIPrinterRecognitionConfig m_config;
    std::unique_ptr<Agent::IAgentHttpTransport> m_transport;
    std::mutex m_mutex;
    std::deque<HttpItem> m_http;
    std::deque<RecognitionEvent> m_events;
    std::string m_body;
    std::uint64_t m_generation{0};
    bool m_busy{false};
};

// Used by tests and explicit developer mode. It follows the same poll-based
// contract and never invents an identifier outside the supplied catalogue.
class DeterministicPrinterRecognition final : public IPrinterRecognitionService
{
public:
    bool ready() const override { return true; }
    bool start(std::uint64_t generation, const PrinterEvidence& evidence,
               const std::vector<const PrinterCandidate*>& choices) override;
    void cancel() override { m_event.reset(); }
    std::optional<RecognitionEvent> poll() override;
private:
    std::optional<RecognitionEvent> m_event;
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
