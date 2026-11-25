#pragma once

// Setting up a model that runs on this computer, in an app such as Ollama or
// LM Studio that answers OpenAI's Responses API at a local address.
//
// Finding asks the app at the address for its models and, where the app is
// one the setup knows, which of them take tools and read pictures. Checking
// is building, as a key check is: one real request with every tool the chat
// offers goes to the model through the agent chats will use, then the app is
// asked how much context it loaded the model with. A model passes when it
// answers and its context holds the app's tools (32k).
//
// GUI-free and poll-driven, like ProviderKeySetup: the app's answers only
// queue, and poll() reads them on the owner's thread. The HTTP client and
// the settings are injected, so the tests drive it with recorded answers.

#include "AgentService.hpp"
#include "AgentTransport.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

// The model on this computer the person set up: where the app that runs it
// answers, the model's name as that app gives it, which app it is ("Ollama",
// "LM Studio", or empty when the app could not tell), and whether the model
// reads pictures.
struct LocalModelRecord
{
    std::string address;
    std::string model;
    std::string runtime;
    bool        images{true};

    bool operator==(const LocalModelRecord& other) const
    {
        return address == other.address && model == other.model && runtime == other.runtime && images == other.images;
    }
};

// One model the app at the address has. `tools` and `images` are unset when
// the app does not say.
struct LocalModelInfo
{
    std::string         id;
    std::optional<bool> tools;
    std::optional<bool> images;
};

struct LocalModelList
{
    std::string                 address; // normalized_local_address()
    std::string                 runtime; // "Ollama", "LM Studio" or empty
    std::vector<LocalModelInfo> models;
};

struct LocalSetupOutcome
{
    enum class Kind { Models, Checked, Failed } kind{Kind::Failed};
    LocalModelList   models;  // Models
    LocalModelRecord record;  // Checked: stored already
    AgentServicePtr  service; // Checked: idle, ready to install
    AgentError       error;   // Failed: local_unreachable() or local_cant_run()
    int              elapsed_ms{0};
};

// The smallest context that holds the app's tools and a turn's messages.
inline constexpr long kLocalMinContext = 32768;

inline constexpr const char* kRuntimeOllama   = "Ollama";
inline constexpr const char* kRuntimeLmStudio = "LM Studio";

class LocalModelSetup
{
public:
    // What the app answered one request with. status 0 with `error` set
    // means nothing answered.
    struct Answer
    {
        unsigned    status{0};
        std::string body;
        std::string error;
    };
    // One request to the app: "GET" or "POST" (with a JSON body). `done` is
    // called once, on any thread.
    using Fetch            = std::function<void(const std::string& method, const std::string& url, const std::string& body,
                                     std::function<void(Answer)> done)>;
    using TransportFactory = std::function<std::unique_ptr<IAgentHttpTransport>()>;
    struct Store
    {
        std::function<std::optional<LocalModelRecord>()> load;
        std::function<void(const LocalModelRecord&)>     store;
        std::function<void()>                            remove;
    };
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    LocalModelSetup(Fetch fetch, TransportFactory transport, Store store, Clock clock = {});
    ~LocalModelSetup();

    bool busy() const { return m_step != Step::Idle; }
    // Lists the models at `address` as the person typed it. Throws when busy.
    void find(const std::string& address);
    // Checks `model`, one of the last list's, at that list's address, and
    // stores it when it passes. Throws when busy or for a model not listed.
    void check(const std::string& model);
    void cancel();
    // At most one finished outcome. Throws for an answer that names nothing
    // the person can act on, after stopping; the caller's top handler
    // reports it.
    std::optional<LocalSetupOutcome> poll();

    std::optional<LocalModelRecord> saved() const;
    void                            remove();

private:
    enum class Step { Idle, Models, Version, Show, LmsModels, Probe, LoadedContext };
    // Where the app's answers wait for poll(). Shared with the requests in
    // flight, which may answer after the setup is gone.
    struct Answers
    {
        std::mutex         mutex;
        std::deque<Answer> queue;
        std::uint64_t      generation{0};
    };

    void                             request(const std::string& method, const std::string& path, const std::string& body = {});
    std::optional<LocalSetupOutcome> advance(const Answer& answer);
    std::optional<LocalSetupOutcome> next_show();
    std::optional<LocalSetupOutcome> start_probe();
    std::optional<LocalSetupOutcome> poll_probe();
    LocalSetupOutcome                failed(AgentError error);
    LocalSetupOutcome                checked();
    [[noreturn]] void                unexpected(const std::string& what, const Answer& answer);
    void                             stop();
    int                              elapsed_ms() const;

    Fetch            m_fetch;
    TransportFactory m_transport;
    Store            m_store;
    Clock            m_clock;

    std::shared_ptr<Answers> m_answers{std::make_shared<Answers>()};

    Step                                  m_step{Step::Idle};
    std::chrono::steady_clock::time_point m_started{};
    LocalModelList                        m_list;
    std::size_t                           m_showing{0}; // the model /api/show is asked about
    std::string                           m_checking;
    AgentServicePtr                       m_candidate;
};

} // namespace Slic3r::GUI::JusPrin::Agent
