// Unit tests for provider credential setup: what the check actually sends,
// what it accepts as proof that a key works, and what it refuses.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/AgentSetup.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProviderProfile.hpp"

#include <nlohmann/json.hpp>

#include <map>
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
    CHECK(request.header("Authorization") == "Bearer sk-secret");

    // A provider the user has not chosen yet must not receive their workspace
    // just to prove that a key works: the probe is one fixed sentence with an
    // empty snapshot and no conversation history.
    const json body = json::parse(request.body, nullptr, false);
    REQUIRE_FALSE(body.is_discarded());
    REQUIRE(body["input"].is_array());
    REQUIRE(body["input"].size() == 1);
    CHECK(body["input"][0]["role"] == "user");

    REQUIRE(body["input"][0]["content"].size() == 2);
    CHECK(body["input"][0]["content"][0]["text"] == "Reply with the single word: ready.");
    // The snapshot travels beside that text; an empty one carries no plates,
    // and so no object or project names to leak.
    REQUIRE(body["input"][0]["content"][1]["text"].is_string());
    const std::string text = body["input"][0]["content"][1]["text"].get<std::string>();
    CHECK(text.rfind("<workspace>", 0) == 0);
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

    SetupCredentials other;
    other.provider = "other";
    other.api_key  = "sk-other-test";
    CHECK_FALSE(fixture.setup->start_check(other));

    SetupCredentials blank = fixture.openai_key("");
    CHECK_FALSE(fixture.setup->start_check(blank));

    CHECK(fixture.log.requests.empty());
    CHECK_FALSE(fixture.setup->busy());
    CHECK(setup_provider_supported("openai"));
    CHECK_FALSE(setup_provider_supported("other"));
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

TEST_CASE("an OpenRouter key is checked against OpenRouter, and only the table decides what is supported", "[agent][setup][openrouter]")
{
    CHECK(setup_provider_supported("openrouter"));
    for (const ProviderProfile& profile : provider_profiles())
        CHECK(setup_provider_supported(profile.id));
    CHECK_FALSE(setup_provider_supported("other"));
    CHECK_FALSE(setup_provider_supported(""));

    Fixture fixture;
    SetupCredentials credentials;
    credentials.provider = "openrouter";
    credentials.api_key  = "sk-or-secret";
    REQUIRE(fixture.setup->start_check(credentials));
    REQUIRE(fixture.log.requests.size() == 1);
    const AgentHttpRequest& request = fixture.log.requests.front();
    CHECK(request.url == "https://openrouter.ai/api/v1/responses");
    CHECK(request.header("Authorization") == "Bearer sk-or-secret");
    CHECK(request.header("X-Title") == "JusPrin");
    // The probe is the same fixed sentence, with the OpenRouter default model.
    const json body = json::parse(request.body);
    CHECK(body["model"] == "openai/gpt-5.4-mini");
    REQUIRE(body["input"].size() == 1);
    CHECK(body["input"][0]["content"][0]["text"] == "Reply with the single word: ready.");

    deliver(fixture.log, AgentHttpEvent{AgentHttpEvent::Kind::Error, {}, "unauthorized", 401});
    const std::optional<SetupOutcome> outcome = fixture.setup->poll();
    REQUIRE(outcome.has_value());
    CHECK_FALSE(outcome->ok);
    CHECK(outcome->error.message.find("The OpenRouter API key was rejected") == 0);
}

TEST_CASE("the endpoint override redirects whichever provider is being checked", "[agent][setup][openrouter]")
{
    TransportLog log;
    ProviderKeySetup setup([&log]() -> std::unique_ptr<IAgentHttpTransport> { return std::make_unique<FakeTransport>(log); },
                           [](const SetupCredentials&) { return true; }, {}, "http://127.0.0.1:1/v1/responses");
    SetupCredentials credentials;
    credentials.provider = "openrouter";
    credentials.api_key  = "k";
    REQUIRE(setup.start_check(credentials));
    CHECK(log.requests.back().url == "http://127.0.0.1:1/v1/responses");
    setup.cancel();
    // An endpoint the page sends wins over the override, as before.
    credentials.endpoint = "http://127.0.0.1:2/v1/responses";
    REQUIRE(setup.start_check(credentials));
    CHECK(log.requests.back().url == "http://127.0.0.1:2/v1/responses");
}

// The setup screen is told a stored key exists through its hint. However the
// key is shaped, the hint is the provider's fixed start, an ellipsis, and at
// most the key's last 4 characters: nothing else of the key.
TEST_CASE("a key's hint is its known start and last 4 characters, never more", "[agent][setup][saved_keys]")
{
    const std::string ellipsis = "…";
    CHECK(api_key_hint("anthropic", "sk-ant-api03-TESTONLY000000000000000a3F9") == "sk-ant-" + ellipsis + "a3F9");
    CHECK(api_key_hint("openai", "sk-proj-TESTONLY0000000000000000wxyz") == "sk-proj-" + ellipsis + "wxyz");
    CHECK(api_key_hint("openai", "sk-TESTONLY0000000000000000wxyz") == "sk-" + ellipsis + "wxyz");
    CHECK(api_key_hint("openrouter", "sk-or-v1-TESTONLY000000000000000000beef") == "sk-or-v1-" + ellipsis + "beef");
    // A start the provider does not use is part of the key, so it is hidden.
    CHECK(api_key_hint("anthropic", "TESTONLY-0000000000000000000000c0de") == ellipsis + "c0de");
    CHECK(api_key_hint("other", "sk-ant-TESTONLY0000000000000000c0de") == ellipsis + "c0de");
    // Too short to give away 4 characters: the end stays hidden too.
    CHECK(api_key_hint("anthropic", "sk-ant-TESTONL") == "sk-ant-" + ellipsis);
    CHECK(api_key_hint("openai", "short") == ellipsis);
    CHECK(api_key_hint("openai", "") == ellipsis);

    // Every length, every provider: what the hint shows of the key is a known
    // start and the last 4 characters, and only for a key that keeps at least
    // 8 characters hidden.
    const std::vector<std::pair<std::string, std::string>> starts{
        {"anthropic", "sk-ant-"}, {"openai", "sk-proj-"}, {"openai", "sk-"}, {"openrouter", "sk-or-v1-"}, {"openai", ""}};
    for (const auto& [provider, start] : starts)
        for (std::size_t length = 0; length < 48; ++length) {
            std::string body;
            for (std::size_t i = 0; i < length; ++i)
                body += static_cast<char>('A' + i % 26);
            const std::string key  = start + body;
            const std::string hint = api_key_hint(provider, key);
            CAPTURE(provider, key, hint);
            const std::size_t mark = hint.find(ellipsis);
            REQUIRE(mark != std::string::npos);
            const std::string shown_start = hint.substr(0, mark);
            const std::string shown_end   = hint.substr(mark + ellipsis.size());
            CHECK(key.compare(0, shown_start.size(), shown_start) == 0);
            CHECK(shown_start.size() <= start.size());
            CHECK(shown_end.size() <= 4);
            CHECK(key.size() >= shown_end.size());
            CHECK(key.compare(key.size() - shown_end.size(), shown_end.size(), shown_end) == 0);
            if (!shown_end.empty())
                CHECK(key.size() - shown_start.size() - shown_end.size() >= 8);
        }
}

TEST_CASE("setup lists stored keys by hint and Remove deletes one", "[agent][setup][saved_keys]")
{
    std::map<std::string, std::string> stored{{"anthropic", "sk-ant-api03-TESTONLY000000000000000a3F9"},
                                              {"openai", "sk-proj-TESTONLY0000000000000000wxyz"}};
    ProviderKeySetup::KeyStore keys;
    keys.load = [&stored](const std::string& provider) {
        const auto key = stored.find(provider);
        return key == stored.end() ? std::string() : key->second;
    };
    keys.remove = [&stored](const std::string& provider) { return stored.erase(provider) > 0; };
    TransportLog     log;
    ProviderKeySetup setup([&log]() -> std::unique_ptr<IAgentHttpTransport> { return std::make_unique<FakeTransport>(log); },
                           [](const SetupCredentials&) { return true; }, keys);

    // In setup's order, by hint.
    std::vector<SavedKey> saved = setup.saved_keys();
    REQUIRE(saved.size() == 2);
    CHECK(saved[0].provider == "openai");
    CHECK(saved[0].hint == "sk-proj-…wxyz");
    CHECK(saved[1].provider == "anthropic");
    CHECK(saved[1].hint == "sk-ant-…a3F9");

    REQUIRE(setup.remove_key("anthropic"));
    CHECK(stored.count("anthropic") == 0);
    saved = setup.saved_keys();
    REQUIRE(saved.size() == 1);
    CHECK(saved[0].provider == "openai");
    // Nothing stored, or a provider this build does not have, is not removed.
    CHECK_FALSE(setup.remove_key("anthropic"));
    CHECK_FALSE(setup.remove_key("openai_api_key"));
    CHECK(stored.count("openai") == 1);
    // No request went anywhere.
    CHECK(log.requests.empty());
}
