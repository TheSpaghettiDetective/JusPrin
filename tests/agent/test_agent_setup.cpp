// Unit tests for provider credential setup: what the check actually sends,
// what it accepts as proof that a key works, and what it refuses.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/AgentSetup.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <vector>

using namespace Slic3r::GUI::JusPrin::Agent;
using nlohmann::json;

namespace {

// One check builds one transport, so the fake records into a shared log the
// test owns rather than into the instance the factory hands away.
struct TransportLog
{
    std::vector<AgentHttpRequest>             requests;
    std::vector<IAgentHttpTransport::EventFn> callbacks;
    std::vector<std::shared_ptr<bool>>        cancelled; // parallel to callbacks
    int                                       cancels{0};
};

// Mirrors the real transport on the one behavior that matters here: once
// cancelled it delivers nothing further, so an abandoned check cannot call
// back into a service that has already been torn down.
class FakeTransport final : public IAgentHttpTransport
{
public:
    explicit FakeTransport(TransportLog& log) : m_log(log) {}

    bool post(AgentHttpRequest request, EventFn event) override
    {
        m_log.requests.push_back(std::move(request));
        m_log.callbacks.push_back(std::move(event));
        m_log.cancelled.push_back(m_cancelled);
        return true;
    }
    void cancel() override
    {
        *m_cancelled = true;
        ++m_log.cancels;
    }

private:
    TransportLog&         m_log;
    std::shared_ptr<bool> m_cancelled{std::make_shared<bool>(false)};
};

void deliver(TransportLog& log, AgentHttpEvent event)
{
    if (log.callbacks.empty() || *log.cancelled.back())
        return;
    log.callbacks.back()(std::move(event));
}

std::string sse(const json& event) { return "event: message\ndata: " + event.dump() + "\n\n"; }

// A provider reply that says nothing interesting and finishes cleanly.
void answer_ok(TransportLog& log)
{
    deliver(log, AgentHttpEvent{AgentHttpEvent::Kind::Data,
                                sse(json{{"type", "response.output_text.delta"}, {"delta", "ready"}}), {}, 0});
    deliver(log, AgentHttpEvent{AgentHttpEvent::Kind::Data,
                                sse(json{{"type", "response.completed"}, {"response", json{{"output", json::array()}}}}),
                                {}, 0});
    deliver(log, AgentHttpEvent{AgentHttpEvent::Kind::Complete, {}, {}, 200});
}

struct Fixture
{
    TransportLog                       log;
    std::vector<SetupCredentials>      committed;
    bool                               commit_succeeds{true};
    std::unique_ptr<ProviderKeySetup>  setup;

    Fixture()
    {
        setup = std::make_unique<ProviderKeySetup>(
            [this]() -> std::unique_ptr<IAgentHttpTransport> { return std::make_unique<FakeTransport>(log); },
            [this](const SetupCredentials& credentials) {
                committed.push_back(credentials);
                return commit_succeeds;
            });
    }

    SetupCredentials openai_key(std::string key = "sk-test-key") const
    {
        SetupCredentials credentials;
        credentials.provider = "openai";
        credentials.api_key  = std::move(key);
        return credentials;
    }
};

} // namespace

TEST_CASE("a key that the provider answers is verified and hands back a live service", "[agent][setup]")
{
    Fixture fixture;

    REQUIRE(fixture.setup->start_check(fixture.openai_key()));
    CHECK(fixture.setup->busy());
    // Nothing is decided until the provider answers.
    CHECK_FALSE(fixture.setup->poll().has_value());

    answer_ok(fixture.log);
    const std::optional<SetupOutcome> outcome = fixture.setup->poll();
    REQUIRE(outcome.has_value());
    CHECK(outcome->ok);
    REQUIRE(outcome->service != nullptr);
    // The verified service is handed over ready for a first real message, not
    // still holding the probe turn.
    CHECK(outcome->service->ready());
    CHECK_FALSE(outcome->service->busy());
    CHECK_FALSE(fixture.setup->busy());
}

TEST_CASE("the check carries the candidate key and no project data", "[agent][setup][privacy]")
{
    Fixture fixture;
    REQUIRE(fixture.setup->start_check(fixture.openai_key("sk-secret")));

    REQUIRE(fixture.log.requests.size() == 1);
    const AgentHttpRequest& request = fixture.log.requests.front();
    CHECK(request.authorization == "Bearer sk-secret");

    // A provider the user has not chosen yet must not receive their workspace
    // just to prove that a key works: the probe is one fixed sentence with an
    // empty snapshot and no conversation history.
    const json body = json::parse(request.body, nullptr, false);
    REQUIRE_FALSE(body.is_discarded());
    REQUIRE(body["input"].is_array());
    REQUIRE(body["input"].size() == 1);
    CHECK(body["input"][0]["role"] == "user");

    REQUIRE(body["input"][0]["content"][0]["text"].is_string());
    const std::string text = body["input"][0]["content"][0]["text"].get<std::string>();
    CHECK(text.find("Reply with the single word") != std::string::npos);
    // The snapshot travels inside that text; an empty one carries no plates,
    // and so no object or project names to leak.
    CHECK(text.find("\"plates\":[]") != std::string::npos);
    CHECK(text.find("\"projectName\":\"\"") != std::string::npos);
}

TEST_CASE("a rejected key reports why and leaves nothing configured", "[agent][setup]")
{
    Fixture fixture;
    REQUIRE(fixture.setup->start_check(fixture.openai_key("sk-wrong")));

    deliver(fixture.log, AgentHttpEvent{AgentHttpEvent::Kind::Error, {}, "unauthorized", 401});
    const std::optional<SetupOutcome> outcome = fixture.setup->poll();
    REQUIRE(outcome.has_value());
    CHECK_FALSE(outcome->ok);
    CHECK(outcome->error.code == "invalid_credentials");
    CHECK(outcome->service == nullptr);
    CHECK(fixture.committed.empty());
    CHECK_FALSE(fixture.setup->busy());
}

TEST_CASE("setup refuses what this build cannot verify", "[agent][setup]")
{
    Fixture fixture;

    SetupCredentials anthropic;
    anthropic.provider = "anthropic";
    anthropic.api_key  = "sk-ant-test";
    CHECK_FALSE(fixture.setup->start_check(anthropic));

    SetupCredentials blank = fixture.openai_key("");
    CHECK_FALSE(fixture.setup->start_check(blank));

    CHECK(fixture.log.requests.empty());
    CHECK_FALSE(fixture.setup->busy());
    CHECK(setup_provider_supported("openai"));
    CHECK_FALSE(setup_provider_supported("anthropic"));
}

TEST_CASE("a second check cannot start while one is in flight", "[agent][setup]")
{
    Fixture fixture;
    REQUIRE(fixture.setup->start_check(fixture.openai_key()));
    CHECK_FALSE(fixture.setup->start_check(fixture.openai_key("sk-other")));
    CHECK(fixture.log.requests.size() == 1);
}

TEST_CASE("cancelling a check abandons it and the provider connection", "[agent][setup]")
{
    Fixture fixture;
    REQUIRE(fixture.setup->start_check(fixture.openai_key()));

    fixture.setup->cancel();
    CHECK_FALSE(fixture.setup->busy());
    CHECK(fixture.log.cancels >= 1);

    // A late answer to the abandoned check produces no outcome.
    answer_ok(fixture.log);
    CHECK_FALSE(fixture.setup->poll().has_value());
}

TEST_CASE("committing reports whether the credential was actually stored", "[agent][setup]")
{
    Fixture fixture;
    CHECK(fixture.setup->commit(fixture.openai_key("sk-stored")));
    REQUIRE(fixture.committed.size() == 1);
    CHECK(fixture.committed.front().api_key == "sk-stored");

    fixture.commit_succeeds = false;
    CHECK_FALSE(fixture.setup->commit(fixture.openai_key()));
}
