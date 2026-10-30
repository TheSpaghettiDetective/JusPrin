// Unit tests for the JusPrin account provider: signing in (what the browser
// is sent to, what makes a token good enough to keep), the account row's
// stored state, and what the gateway is sent on every request.

#include <catch2/catch_all.hpp>

#include "kept_turn.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentSetup.hpp"
#include "slic3r/GUI/JusPrin/Agent/CloudAccount.hpp"
#include "slic3r/GUI/JusPrin/Agent/ModelCatalog.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProviderFactory.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProviderProfile.hpp"

#include <nlohmann/json.hpp>

#include <map>
#include <memory>
#include <vector>

using namespace Slic3r::GUI::JusPrin::Agent;
using Catch::Matchers::ContainsSubstring;
using nlohmann::json;

namespace {

const char* kGateway = "https://jusprin.test/ent/jusprin/v2/responses";

class IdleAgent final : public IAgentService
{
public:
    bool                      ready() const override { return true; }
    bool                      busy() const override { return false; }
    bool                      start(const AgentRequest&) override { return true; }
    bool                      continue_after_tool(const AgentToolResult&) override { return false; }
    void                      cancel() override {}
    std::optional<AgentEvent> poll() override { return std::nullopt; }
};

// A browser and a server the test answers by hand.
struct FakeMachine
{
    std::vector<CloudAuthorization>                     authorizations;
    std::function<void(CloudAuthorizationResult)>       browser;
    bool                                                port_free{true};
    int                                                 cancels{0};
    int                                                 pumps{0};
    std::map<std::string, std::function<void(CloudHttpResult)>> asked; // url -> answer
    std::vector<std::string>                            tokens;        // bearer of each GET
    std::vector<std::pair<std::string, std::string>>    revoked;       // url, token

    CloudAccountDeps deps()
    {
        CloudAccountDeps deps;
        deps.authorize = [this](const CloudAuthorization& authorization, std::function<void(CloudAuthorizationResult)> done) {
            if (!port_free)
                return false;
            authorizations.push_back(authorization);
            browser = std::move(done);
            return true;
        };
        deps.cancel_authorize = [this] { ++cancels; };
        deps.pump             = [this] { ++pumps; };
        deps.get = [this](const std::string& url, const std::string& token, std::function<void(CloudHttpResult)> done) {
            asked[url] = std::move(done);
            tokens.push_back(token);
        };
        deps.revoke = [this](const std::string& url, const std::string&, const std::string& token) { revoked.emplace_back(url, token); };
        deps.pkce   = [](std::string& verifier, std::string& challenge, std::string& state) {
            verifier  = "the-verifier";
            challenge = "the-challenge";
            state     = "the-state";
        };
        return deps;
    }

    void answer(const std::string& url, unsigned status, const std::string& body)
    {
        REQUIRE(asked.count(url) == 1);
        auto done = std::move(asked[url]);
        asked.erase(url);
        done({status, body});
    }
};

const std::string kWho    = "https://jusprin.test/jusprin/api/me/";
const std::string kModels = "https://jusprin.test/ent/jusprin/v2/models";

// /jusprin/api/me/ as the server answers it.
std::string me_body(const std::string& email)
{
    return json{{"user", {{"id", 7}, {"username", email}, {"email", email}, {"first_name", ""}, {"last_name", ""}}},
                {"ai_credits", {{"monthly_limit", 100}, {"used_credits", 3}}}}
        .dump();
}

std::string models_body(const std::vector<std::pair<std::string, std::string>>& models)
{
    json data = json::array();
    for (const auto& [id, format] : models)
        data.push_back(json{{"id", id}, {"label", id}, {"format", format}, {"efforts", {"fast", "thorough"}}});
    return json{{"data", data}}.dump();
}

struct Fixture
{
    FakeMachine                                      machine;
    std::optional<CloudAccountRecord>                stored;
    bool                                             save_succeeds{true};
    int                                              erases{0};
    std::vector<std::tuple<std::string, std::string, std::string>> built; // token, model, format
    std::unique_ptr<CloudSignIn>                     sign_in;

    Fixture()
    {
        CloudAccountStore store;
        store.load = [this] { return stored; };
        store.save = [this](const CloudAccountRecord& record) {
            if (save_succeeds)
                stored = record;
            return save_succeeds;
        };
        store.erase = [this] {
            ++erases;
            stored.reset();
            return true;
        };
        sign_in = std::make_unique<CloudSignIn>(cloud_server_from_gateway(kGateway), kGateway, "slicer-1", machine.deps(),
                                                std::move(store),
                                                [this](const std::string& token, const std::string& model, const std::string& format) {
                                                    built.emplace_back(token, model, format);
                                                    return std::make_unique<IdleAgent>();
                                                });
    }

    // The browser comes back with a token.
    void browser_returns(const std::string& token)
    {
        REQUIRE(machine.browser);
        machine.browser({true, token, {}});
        CHECK_FALSE(sign_in->poll().has_value());
        CHECK(sign_in->phase() == CloudSignInPhase::Checking);
    }
};

} // namespace

TEST_CASE("signing in sends the browser to the authorize page with PKCE and a loopback redirect", "[agent][account]")
{
    Fixture f;
    REQUIRE(f.sign_in->start());
    CHECK(f.sign_in->phase() == CloudSignInPhase::Browser);
    REQUIRE(f.machine.authorizations.size() == 1);
    const CloudAuthorization& sent = f.machine.authorizations.front();
    CHECK(sent.client_id == "JusPrin2");
    const std::string port = std::to_string(kCloudCallbackPort);
    CHECK(sent.port == kCloudCallbackPort);
    CHECK(sent.redirect_uri == "http://127.0.0.1:" + port + "/callback");
    CHECK(sent.token_url == "https://jusprin.test/o/token/");
    CHECK(sent.code_verifier == "the-verifier");
    CHECK(sent.state == "the-state");
    CHECK_THAT(sent.authorize_url, ContainsSubstring("https://jusprin.test/o/authorize/?response_type=code&client_id=JusPrin2"));
    CHECK_THAT(sent.authorize_url, ContainsSubstring("redirect_uri=http%3A%2F%2F127.0.0.1%3A" + port + "%2Fcallback"));
    CHECK_THAT(sent.authorize_url, ContainsSubstring("scope=read%20write"));
    CHECK_THAT(sent.authorize_url, ContainsSubstring("code_challenge=the-challenge&code_challenge_method=S256"));
    // The verifier never travels in the browser.
    CHECK_THAT(sent.authorize_url, !ContainsSubstring("the-verifier"));
    CHECK(f.sign_in->browser_url() == sent.authorize_url);
    // One at a time.
    CHECK_FALSE(f.sign_in->start());
}

TEST_CASE("a sign-in that cannot listen for the browser does not start", "[agent][account]")
{
    Fixture f;
    f.machine.port_free = false;
    CHECK_FALSE(f.sign_in->start());
    CHECK(f.sign_in->phase() == CloudSignInPhase::Idle);
}

TEST_CASE("a token the browser brings back is checked, stored and handed over as an agent", "[agent][account]")
{
    Fixture f;
    REQUIRE(f.sign_in->start());
    f.browser_returns("tok-1");

    // Who the account is, and which models it is served, both with the token.
    CHECK(f.machine.tokens == std::vector<std::string>{"tok-1", "tok-1"});
    f.machine.answer(kWho, 200, me_body("alex@example.com"));
    CHECK_FALSE(f.sign_in->poll().has_value());
    f.machine.answer(kModels, 200, models_body({{"slicer-1", "anthropic"}, {"slicer-2", "responses"}}));

    const std::optional<CloudSignInOutcome> outcome = f.sign_in->poll();
    REQUIRE(outcome.has_value());
    CHECK(outcome->kind == CloudSignInOutcome::Kind::SignedIn);
    CHECK(outcome->email == "alex@example.com");
    CHECK(outcome->saved);
    CHECK(outcome->service != nullptr);
    CHECK(f.sign_in->phase() == CloudSignInPhase::Idle);
    REQUIRE(f.stored.has_value());
    CHECK(f.stored->token == "tok-1");
    CHECK(f.stored->email == "alex@example.com");
    CHECK(f.stored->formats == std::vector<std::pair<std::string, std::string>>{{"slicer-1", "anthropic"}, {"slicer-2", "responses"}});
    // The agent is for the provider's default model, in its format.
    REQUIRE(f.built.size() == 1);
    CHECK(f.built.front() == std::make_tuple(std::string("tok-1"), std::string("slicer-1"), std::string("anthropic")));
    CHECK(f.machine.pumps > 0);
}

TEST_CASE("an account without the default model gets the first one listed", "[agent][account]")
{
    Fixture f;
    REQUIRE(f.sign_in->start());
    f.browser_returns("tok-1");
    f.machine.answer(kModels, 200, models_body({{"slicer-2", "responses"}, {"slicer-3", "anthropic"}}));
    f.machine.answer(kWho, 200, me_body("alex@example.com"));
    REQUIRE(f.sign_in->poll().has_value());
    REQUIRE(f.built.size() == 1);
    CHECK(std::get<1>(f.built.front()) == "slicer-2");
    CHECK(std::get<2>(f.built.front()) == "responses");
}

TEST_CASE("an account with no models cannot sign in, and nothing is stored", "[agent][account]")
{
    Fixture f;
    REQUIRE(f.sign_in->start());
    f.browser_returns("tok-1");
    f.machine.answer(kWho, 200, me_body("alex@example.com"));
    f.machine.answer(kModels, 200, models_body({}));
    const std::optional<CloudSignInOutcome> outcome = f.sign_in->poll();
    REQUIRE(outcome.has_value());
    CHECK(outcome->kind == CloudSignInOutcome::Kind::Failed);
    CHECK(outcome->error.code == "account_no_models");
    CHECK_FALSE(f.stored.has_value());
}

TEST_CASE("a sign-in the browser did not finish fails and stores nothing", "[agent][account]")
{
    Fixture f;
    REQUIRE(f.sign_in->start());
    f.machine.browser({false, {}, "access_denied"});
    const std::optional<CloudSignInOutcome> outcome = f.sign_in->poll();
    REQUIRE(outcome.has_value());
    CHECK(outcome->kind == CloudSignInOutcome::Kind::Failed);
    CHECK(outcome->error.code == "sign_in_failed");
    CHECK(outcome->error.retryable);
    CHECK(f.machine.asked.empty());
    CHECK_FALSE(f.stored.has_value());
    CHECK(f.sign_in->phase() == CloudSignInPhase::Idle);
}

TEST_CASE("a server that cannot be reached leaves the account as it was", "[agent][account]")
{
    Fixture f;
    REQUIRE(f.sign_in->start());
    f.browser_returns("tok-1");
    f.machine.answer(kWho, 0, "");
    f.machine.answer(kModels, 503, "");
    const std::optional<CloudSignInOutcome> outcome = f.sign_in->poll();
    REQUIRE(outcome.has_value());
    CHECK(outcome->kind == CloudSignInOutcome::Kind::Failed);
    CHECK(outcome->error.code == "account_unreachable");
    CHECK_FALSE(f.stored.has_value());
}

TEST_CASE("a stored sign-in the server refuses is reported expired and kept", "[agent][account]")
{
    Fixture f;
    f.stored = CloudAccountRecord{"old-token", "alex@example.com", {{"slicer-1", "anthropic"}}};
    REQUIRE(f.sign_in->check_stored());
    CHECK(f.sign_in->phase() == CloudSignInPhase::Checking);
    CHECK(f.machine.authorizations.empty()); // no browser
    f.machine.answer(kWho, 401, "{}");
    f.machine.answer(kModels, 401, "{}");
    const std::optional<CloudSignInOutcome> outcome = f.sign_in->poll();
    REQUIRE(outcome.has_value());
    CHECK(outcome->kind == CloudSignInOutcome::Kind::Expired);
    // Signing in again replaces it; nothing is forgotten behind the person's back.
    CHECK(f.erases == 0);
    REQUIRE(f.stored.has_value());
    CHECK(f.stored->token == "old-token");
}

TEST_CASE("a stored sign-in that still works refreshes the account's formats", "[agent][account]")
{
    Fixture f;
    f.stored = CloudAccountRecord{"old-token", "", {{"slicer-1", "anthropic"}}};
    REQUIRE(f.sign_in->check_stored());
    f.machine.answer(kWho, 200, me_body("alex@example.com"));
    f.machine.answer(kModels, 200, models_body({{"slicer-1", "responses"}}));
    const std::optional<CloudSignInOutcome> outcome = f.sign_in->poll();
    REQUIRE(outcome.has_value());
    CHECK(outcome->kind == CloudSignInOutcome::Kind::SignedIn);
    CHECK(f.stored->email == "alex@example.com");
    CHECK(f.stored->formats == std::vector<std::pair<std::string, std::string>>{{"slicer-1", "responses"}});
}

TEST_CASE("nothing to check without a stored sign-in", "[agent][account]")
{
    Fixture f;
    CHECK_FALSE(f.sign_in->check_stored());
    f.stored = CloudAccountRecord{};
    CHECK_FALSE(f.sign_in->check_stored());
}

TEST_CASE("cancelling a sign-in drops what the browser or server says afterwards", "[agent][account]")
{
    Fixture f;
    REQUIRE(f.sign_in->start());
    auto late_browser = f.machine.browser;
    f.sign_in->cancel();
    CHECK(f.machine.cancels == 1);
    CHECK(f.sign_in->phase() == CloudSignInPhase::Idle);
    late_browser({true, "tok-late", {}});
    CHECK_FALSE(f.sign_in->poll().has_value());
    CHECK(f.machine.asked.empty());

    // The same for a check already talking to the server.
    f.stored = CloudAccountRecord{"old-token", "", {}};
    REQUIRE(f.sign_in->check_stored());
    f.sign_in->cancel();
    f.machine.answer(kWho, 200, me_body("alex@example.com"));
    f.machine.answer(kModels, 200, models_body({{"slicer-1", "anthropic"}}));
    CHECK_FALSE(f.sign_in->poll().has_value());
    CHECK(f.built.empty());
}

TEST_CASE("signing out forgets the account and revokes its token", "[agent][account]")
{
    Fixture f;
    CHECK_FALSE(f.sign_in->sign_out());
    f.stored = CloudAccountRecord{"tok-1", "alex@example.com", {}};
    REQUIRE(f.sign_in->account().has_value());
    CHECK(f.sign_in->account()->email == "alex@example.com");
    REQUIRE(f.sign_in->sign_out());
    CHECK(f.erases == 1);
    CHECK_FALSE(f.sign_in->account().has_value());
    CHECK(f.machine.revoked == std::vector<std::pair<std::string, std::string>>{{"https://jusprin.test/o/revoke_token/", "tok-1"}});
}

TEST_CASE("the server is everything before the gateway's /ent/ path", "[agent][account]")
{
    CHECK(cloud_server_from_gateway("https://app.obico.io/ent/jusprin/v2/responses") == "https://app.obico.io");
    CHECK(cloud_server_from_gateway("http://localhost:3334/ent/jusprin/v2/messages") == "http://localhost:3334");
    CHECK(cloud_server_from_gateway("http://127.0.0.1:9000/v1/responses") == "http://127.0.0.1:9000");
}

// -- The setup screen's view of the account ----------------------------------

namespace {

// A sign-in that only reports a stored account and records sign-outs.
class StoredAccount final : public ICloudSignIn
{
public:
    std::optional<CloudAccountRecord> record;
    int                               sign_outs{0};

    CloudSignInPhase                  phase() const override { return CloudSignInPhase::Idle; }
    std::string                       browser_url() const override { return {}; }
    bool                              start() override { return false; }
    bool                              check_stored() override { return false; }
    void                              cancel() override {}
    std::optional<CloudSignInOutcome> poll() override { return std::nullopt; }
    std::optional<CloudAccountRecord> account() const override { return record; }
    bool                              sign_out() override
    {
        ++sign_outs;
        record.reset();
        return true;
    }
};

} // namespace

TEST_CASE("setup lists the account by email, signs out for Remove and never checks it as a key", "[agent][account][setup]")
{
    auto account = std::make_shared<StoredAccount>();
    account->record = CloudAccountRecord{"tok-0123456789abcdef", "alex@example.com", {}};
    std::map<std::string, std::string> keys{{"jusprin", "tok-0123456789abcdef"}, {"openai", "sk-proj-0123456789abcdef"}};
    ProviderKeySetup::KeyStore store;
    store.load   = [&keys](const std::string& provider) { return keys.count(provider) ? keys[provider] : std::string(); };
    store.remove = [&keys](const std::string& provider) { return keys.erase(provider) == 1; };
    ProviderKeySetup setup([]() -> std::unique_ptr<IAgentHttpTransport> { return nullptr; },
                           [](const SetupCredentials&) { return true; }, store, {}, {}, account);

    REQUIRE(setup.cloud_sign_in() == account.get());
    const std::vector<SavedKey> saved = setup.saved_keys();
    REQUIRE(saved.size() == 2);
    CHECK(saved[0].provider == "jusprin");
    CHECK(saved[0].account == "alex@example.com");
    CHECK(saved[1].provider == "openai");
    CHECK(saved[1].account.empty());

    // An account's credential comes from signing in.
    SetupCredentials pasted;
    pasted.provider = "jusprin";
    pasted.api_key  = "tok-pasted";
    CHECK_FALSE(setup.start_check(pasted));

    REQUIRE(setup.remove_key("jusprin"));
    CHECK(account->sign_outs == 1);
    CHECK(keys.count("jusprin") == 1); // the sign-in's store forgets it, not the key store
}

// -- The gateway on the wire --------------------------------------------------

TEST_CASE("an account model's format decides its family and endpoint", "[agent][account]")
{
    const ProviderProfile& row = account_provider();
    CHECK(row.id == "jusprin");
    CHECK(row.model == "slicer-1");

    // With no endpoint given, the row's own, its last segment swapped (the
    // gateway's domain is the row's to decide).
    const std::string gateway = row.endpoint.substr(0, row.endpoint.rfind('/'));
    const std::optional<ProviderProfile> anthropic = account_profile(row, {}, kFormatAnthropic);
    REQUIRE(anthropic.has_value());
    CHECK(anthropic->family == ProviderFamily::Anthropic);
    CHECK(anthropic->endpoint == gateway + "/messages");
    CHECK(anthropic->auth == AuthStyle::Bearer);
    // The Messages API requires max_tokens; the row carries it.
    CHECK(anthropic->max_tokens == row.max_tokens);
    CHECK(anthropic->max_tokens > 0u);

    const std::optional<ProviderProfile> responses = account_profile(row, "http://localhost:3334/ent/jusprin/v2/responses", kFormatResponses);
    REQUIRE(responses.has_value());
    CHECK(responses->family == ProviderFamily::Responses);
    CHECK(responses->endpoint == "http://localhost:3334/ent/jusprin/v2/responses");

    CHECK_FALSE(account_profile(row, {}, "").has_value());
    CHECK_FALSE(account_profile(row, {}, "gemini").has_value());
}

TEST_CASE("the account's model list is read as the gateway writes it", "[agent][account][models]")
{
    const ProviderProfile& row = account_provider();
    CHECK(model_list_url(row, {}) == row.endpoint.substr(0, row.endpoint.rfind('/')) + "/models");
    const json response = json::parse(R"({"data": [
        {"id": "slicer-1", "label": "Slicer 1", "format": "anthropic", "efforts": ["fast", "thorough"], "default_effort": "fast"},
        {"id": "slicer-x", "label": "Slicer X", "format": "responses"}]})");
    const ModelListPage page = parse_model_list(row, response);
    REQUIRE(page.models.size() == 2);
    CHECK(page.models[0].id == "slicer-1");
    CHECK(page.models[0].label == "Slicer 1");
    CHECK(page.models[0].format == "anthropic");
    CHECK(page.models[0].efforts == std::vector<std::string>{"fast", "thorough"});
    CHECK(page.models[0].default_effort == "fast");
    CHECK(page.models[1].format == "responses");
    CHECK(page.models[1].efforts.empty());
}

TEST_CASE("an account runtime is built in the stored format, or not at all", "[agent][account]")
{
    AgentSettings settings;
    settings.enabled       = true;
    settings.cloud_consent = true;
    settings.provider      = "jusprin";
    const auto transport   = []() -> std::unique_ptr<IAgentHttpTransport> { return nullptr; };

    AgentRuntime signed_out = resolve_agent_runtime(settings, {}, transport);
    CHECK(signed_out.availability == AgentAvailability::Unavailable);
    CHECK(signed_out.unavailable_reason == "Not signed in to a JusPrin account.");

    settings.api_key = "tok-1";
    AgentRuntime unchecked = resolve_agent_runtime(settings, {}, transport);
    CHECK(unchecked.service == nullptr);
    CHECK(unchecked.unavailable_reason == "Sign in to your JusPrin account again.");
}

namespace {

struct RecordingTransport final : IAgentHttpTransport
{
    std::vector<AgentHttpRequest>* requests;
    std::vector<EventFn>*          callbacks;
    bool post(AgentHttpRequest request, EventFn event) override
    {
        requests->push_back(std::move(request));
        callbacks->push_back(std::move(event));
        return true;
    }
    void cancel() override {}
};

AgentServicePtr account_agent(const std::string& format, std::vector<AgentHttpRequest>& requests,
                              std::vector<IAgentHttpTransport::EventFn>& callbacks)
{
    auto transport       = std::make_unique<RecordingTransport>();
    transport->requests  = &requests;
    transport->callbacks = &callbacks;
    return make_provider_agent(*account_profile(account_provider(), {}, format), "tok-1", "slicer-1", {}, std::move(transport));
}

std::optional<AgentEvent> drain(IAgentService& agent)
{
    std::optional<AgentEvent> last;
    while (std::optional<AgentEvent> event = agent.poll())
        last = std::move(event);
    return last;
}

} // namespace

TEST_CASE("every gateway request names its turn and its kind, with the token as bearer", "[agent][account]")
{
    for (const char* format : {kFormatAnthropic, kFormatResponses}) {
        DYNAMIC_SECTION("format " << format)
        {
            std::vector<AgentHttpRequest>             requests;
            std::vector<IAgentHttpTransport::EventFn> callbacks;
            AgentServicePtr                           agent = account_agent(format, requests, callbacks);
            AgentRequest                              request;
            JusPrinTest::kept_nowhere(request);
            request.request_id   = "project-chat-reply-attempt-1";
            request.user_text    = "hello";
            REQUIRE(agent->start(request));
            REQUIRE(requests.size() == 1);
            CHECK(requests[0].header("Authorization") == "Bearer tok-1");
            CHECK(requests[0].header("X-JusPrin-Turn") == "project-chat-reply-attempt-1");
            CHECK(requests[0].header("X-JusPrin-Request") == "reply");
            CHECK(requests[0].header("x-api-key").empty());
            CHECK(json::parse(requests[0].body)["model"] == "slicer-1");
        }
    }
}

TEST_CASE("every gateway reply names its chat with 32 hex digits from the project and chat ids", "[agent][account]")
{
    const auto chat_header = [](const std::string& chat_key) {
        std::vector<AgentHttpRequest>             requests;
        std::vector<IAgentHttpTransport::EventFn> callbacks;
        AgentServicePtr                           agent = account_agent(kFormatAnthropic, requests, callbacks);
        AgentRequest                              request;
        JusPrinTest::kept_nowhere(request);
        request.request_id = "turn";
        request.chat_key   = chat_key;
        request.user_text  = "hello";
        REQUIRE(agent->start(request));
        REQUIRE(requests.size() == 1);
        return requests[0].header("X-JusPrin-Chat");
    };
    // SHA-256 of the chat key, cut to the 32 lowercase hex digits the gateway accepts.
    CHECK(chat_header("p-1a2b/c-1") == "b60594c00cde61a698a870d314554e00");
    CHECK(chat_header("p-1a2b/c-2") == "64891e5e480ad01d66c439d2c3d52ce4");
    // Titles carry no chat key, and so no chat.
    CHECK(chat_header("").empty());
}

TEST_CASE("the gateway's refusals read as the account's, not a key's", "[agent][account]")
{
    const auto failure = [](unsigned status) {
        std::vector<AgentHttpRequest>             requests;
        std::vector<IAgentHttpTransport::EventFn> callbacks;
        AgentServicePtr                           agent = account_agent(kFormatAnthropic, requests, callbacks);
        AgentRequest                              request;
        JusPrinTest::kept_nowhere(request);
        request.request_id   = "turn";
        request.user_text    = "hello";
        REQUIRE(agent->start(request));
        callbacks.back()(AgentHttpEvent{AgentHttpEvent::Kind::Error, {}, "HTTP error", status});
        std::optional<AgentEvent> event = drain(*agent);
        REQUIRE(event.has_value());
        REQUIRE(event->error.has_value());
        return *event->error;
    };
    const AgentError expired = failure(401);
    CHECK(expired.code == "invalid_credentials");
    CHECK_THAT(expired.message, ContainsSubstring("sign-in has ended"));
    CHECK_FALSE(expired.retryable);
    const AgentError quota = failure(402);
    CHECK(quota.code == "quota_exhausted");
    CHECK_FALSE(quota.retryable);
    // Upstream trouble the gateway reports as its own is the service's.
    CHECK(failure(502).code == "service_unavailable");
}
