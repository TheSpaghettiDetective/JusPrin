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
#include "slic3r/GUI/JusPrin/Agent/RecordConversion.hpp"

#include <nlohmann/json.hpp>

#include <map>
#include <memory>
#include <set>
#include <vector>

using namespace Slic3r::GUI::JusPrin::Agent;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
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
    std::map<std::string, std::function<void(CloudHttpResult)>> posted; // url -> answer
    std::vector<std::pair<std::string, json>>           post_bodies;   // url, body
    std::vector<std::string>                            post_sessions; // X-Session-Token, or empty

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
        deps.post = [this](const std::string& url, const std::string& body, const std::string& session,
                           std::function<void(CloudHttpResult)> done) {
            posted[url] = std::move(done);
            post_bodies.emplace_back(url, json::parse(body));
            post_sessions.push_back(session);
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

    void answer_post(const std::string& url, unsigned status, const std::string& body)
    {
        REQUIRE(posted.count(url) == 1);
        auto done = std::move(posted[url]);
        posted.erase(url);
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

TEST_CASE("signing in sends the browser through Google to the authorize page with PKCE and a loopback redirect", "[agent][account]")
{
    Fixture f;
    REQUIRE(f.sign_in->start(CloudBrowserStart::Google));
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
    // The authorize page's query rides inside Google's "next"; the Google
    // test below reads it there.
    CHECK_THAT(sent.authorize_url, StartsWith("https://jusprin.test/accounts/google/login/?"));
    // The verifier never travels in the browser.
    CHECK_THAT(sent.authorize_url, !ContainsSubstring("the-verifier"));
    CHECK(f.sign_in->browser_url() == sent.authorize_url);
    // One at a time.
    CHECK_FALSE(f.sign_in->start(CloudBrowserStart::Google));
}

TEST_CASE("signing in with Google sends the browser to Google, then on to the authorize page", "[agent][account]")
{
    Fixture f;
    REQUIRE(f.sign_in->start(CloudBrowserStart::Google));
    REQUIRE(f.machine.authorizations.size() == 1);
    const CloudAuthorization& sent = f.machine.authorizations.front();
    // Straight to Google, skipping the server's sign-in page.
    CHECK_THAT(sent.authorize_url, StartsWith("https://jusprin.test/accounts/google/login/?process=login&next="));
    // Back from Google, the server sends the browser on to the same authorize
    // page, so PKCE, state and the loopback redirect are unchanged.
    CHECK_THAT(sent.authorize_url, ContainsSubstring("next=%2Fo%2Fauthorize%2F%3Fresponse_type%3Dcode%26client_id%3DJusPrin2"));
    CHECK_THAT(sent.authorize_url, ContainsSubstring("redirect_uri%3Dhttp%253A%252F%252F127.0.0.1%253A" +
                                                     std::to_string(kCloudCallbackPort) + "%252Fcallback"));
    CHECK_THAT(sent.authorize_url, ContainsSubstring("state%3Dthe-state"));
    CHECK_THAT(sent.authorize_url, ContainsSubstring("code_challenge%3Dthe-challenge%26code_challenge_method%3DS256"));
    CHECK_THAT(sent.authorize_url, !ContainsSubstring("the-verifier"));
    CHECK(sent.redirect_uri == "http://127.0.0.1:" + std::to_string(kCloudCallbackPort) + "/callback");
    CHECK(f.sign_in->browser_url() == sent.authorize_url);
}

TEST_CASE("a sign-in that cannot listen for the browser does not start", "[agent][account]")
{
    Fixture f;
    f.machine.port_free = false;
    CHECK_FALSE(f.sign_in->start(CloudBrowserStart::Google));
    CHECK(f.sign_in->phase() == CloudSignInPhase::Idle);
}

TEST_CASE("a token the browser brings back is checked, stored and handed over as an agent", "[agent][account]")
{
    Fixture f;
    REQUIRE(f.sign_in->start(CloudBrowserStart::Google));
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

TEST_CASE("a sign-in from a release the server no longer serves fails with the server's update words", "[agent][account]")
{
    Fixture f;
    REQUIRE(f.sign_in->start(CloudBrowserStart::Google));
    f.browser_returns("tok-1");
    f.machine.answer(kWho, 200, me_body("alex@example.com"));
    f.machine.answer(kModels, 426, R"({"type":"error","error":{"code":"update_required","message":"Update to 2.1.0."}})");
    const std::optional<CloudSignInOutcome> outcome = f.sign_in->poll();
    REQUIRE(outcome.has_value());
    CHECK(outcome->kind == CloudSignInOutcome::Kind::Failed);
    CHECK(outcome->error.code == "update_required");
    CHECK(outcome->error.message == "Update to 2.1.0.");
    CHECK_FALSE(outcome->error.retryable);
    CHECK_FALSE(f.stored.has_value());
}

TEST_CASE("an account without the default model gets the first one listed", "[agent][account]")
{
    Fixture f;
    REQUIRE(f.sign_in->start(CloudBrowserStart::Google));
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
    REQUIRE(f.sign_in->start(CloudBrowserStart::Google));
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
    REQUIRE(f.sign_in->start(CloudBrowserStart::Google));
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
    REQUIRE(f.sign_in->start(CloudBrowserStart::Google));
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
    REQUIRE(f.sign_in->start(CloudBrowserStart::Google));
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
    CHECK(cloud_server_from_gateway("https://jusprinv2.obico.io/ent/jusprin/v2/responses") == "https://jusprinv2.obico.io");
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
    bool                              start(CloudBrowserStart) override { return false; }
    bool                              check_stored() override { return false; }
    void                              cancel() override {}
    std::optional<CloudSignInOutcome> poll() override { return std::nullopt; }
    std::optional<CloudEmailStep>     email_step() const override { return std::nullopt; }
    bool                              start_email(const std::string&) override { return false; }
    bool                              submit_code(const std::string&) override { return false; }
    bool                              submit_password(const std::string&) override { return false; }
    bool                              send_code() override { return false; }
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

TEST_CASE("every gateway request names its turn, its kind and the JusPrin release, with the token as bearer", "[agent][account]")
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
            // JusPrin's own release, which the OrcaSlicer version it is built on does not tell apart.
            CHECK(requests[0].header("User-Agent") ==
                  std::string("JusPrin/") + Slic3r::GUI::JusPrin::Updates::kVersion + " OrcaSlicer/" + SoftFever_VERSION);
            CHECK(requests[0].header("x-api-key").empty());
            CHECK(json::parse(requests[0].body)["model"] == "slicer-1");
        }
    }
}

TEST_CASE("a turn the app started is named a note to the gateway", "[agent][account]")
{
    std::vector<AgentHttpRequest>             requests;
    std::vector<IAgentHttpTransport::EventFn> callbacks;
    AgentServicePtr                           agent = account_agent(kFormatAnthropic, requests, callbacks);
    AgentRequest                              request;
    JusPrinTest::kept_nowhere(request);
    request.request_id  = "project-chat-reply-attempt-1";
    request.note        = "A slice finished.";
    request.app_started = true;
    REQUIRE(agent->start(request));
    REQUIRE(requests.size() == 1);
    CHECK(requests[0].header("X-JusPrin-Request") == "note");
}

TEST_CASE("the gateway's 402 reaches the page in its own words and numbers", "[agent][account]")
{
    std::vector<AgentHttpRequest>             requests;
    std::vector<IAgentHttpTransport::EventFn> callbacks;
    AgentServicePtr                           agent = account_agent(kFormatResponses, requests, callbacks);
    AgentRequest                              request;
    JusPrinTest::kept_nowhere(request);
    request.request_id = "turn";
    request.user_text  = "hello";
    REQUIRE(agent->start(request));
    const json body = json::parse(R"({"type":"error","error":{"code":"credits_exhausted","message":"This message needs 5 credits; you have 3."},
                                      "balance":3,"cost":5,"plan":null})");
    callbacks.back()(AgentHttpEvent{AgentHttpEvent::Kind::Error, body.dump(), "HTTP error", 402});
    std::optional<AgentEvent> event = drain(*agent);
    REQUIRE(event.has_value());
    REQUIRE(event->error.has_value());
    CHECK(event->error->code == "credits_exhausted");
    CHECK(event->error->message == "This message needs 5 credits; you have 3.");
    CHECK_FALSE(event->error->retryable);
}

TEST_CASE("a release the server no longer serves is told to update, in the server's words, with no retry", "[agent][account]")
{
    const auto failure = [](const std::string& body) {
        std::vector<AgentHttpRequest>             requests;
        std::vector<IAgentHttpTransport::EventFn> callbacks;
        AgentServicePtr                           agent = account_agent(kFormatResponses, requests, callbacks);
        AgentRequest                              request;
        JusPrinTest::kept_nowhere(request);
        request.request_id = "turn";
        request.user_text  = "hello";
        REQUIRE(agent->start(request));
        callbacks.back()(AgentHttpEvent{AgentHttpEvent::Kind::Error, body, "HTTP error", 426});
        std::optional<AgentEvent> event = drain(*agent);
        REQUIRE(event.has_value());
        REQUIRE(event->error.has_value());
        return *event->error;
    };
    const AgentError told = failure(R"({"type":"error","error":{"code":"update_required","message":"Update to 2.1.0."},
                                        "minimumVersion":"2.1.0"})");
    CHECK(told.code == "update_required");
    CHECK(told.message == "Update to 2.1.0.");
    CHECK_FALSE(told.retryable);
    // Without the server's words, the app's own.
    const AgentError bare = failure("");
    CHECK(bare.code == "update_required");
    CHECK_THAT(bare.message, ContainsSubstring("can no longer connect to JusPrin accounts"));
    CHECK_FALSE(bare.retryable);
}

TEST_CASE("a model without eyes is offered no picture tools", "[agent][account]")
{
    const auto offered = [](bool reads_pictures) {
        std::vector<AgentHttpRequest>             requests;
        std::vector<IAgentHttpTransport::EventFn> callbacks;
        AgentServicePtr                           agent = account_agent(kFormatResponses, requests, callbacks);
        AgentRequest                              request;
        JusPrinTest::kept_nowhere(request);
        request.request_id           = "turn";
        request.user_text            = "hello";
        request.model_reads_pictures = reads_pictures;
        REQUIRE(agent->start(request));
        std::set<std::string> names;
        const json body = json::parse(requests[0].body);
        for (const json& tool : body["tools"])
            names.insert(tool.value("name", ""));
        return names;
    };
    const std::set<std::string> with = offered(true);
    const std::set<std::string> without = offered(false);
    CHECK(with.count("view_render") == 1);
    CHECK(with.count("project_attachment_read") == 1);
    CHECK(without.count("view_render") == 0);
    CHECK(without.count("project_attachment_read") == 0);
    CHECK(without.count("slice_start") == 1);
}

// The app's own turns run on Expert (Anthropic's format) in a chat whose model
// is Standard (Responses), so one record holds both.
TEST_CASE("a chat's history crosses wire formats: each model reads what the other wrote", "[agent][account]")
{
    const json anthropic = json::array({json{{"type", "thinking"}, {"thinking", "hm"}, {"signature", "x"}},
                                        json{{"type", "text"}, {"text", "Opened it."}},
                                        json{{"type", "tool_use"}, {"id", "toolu_1"}, {"name", "slice_start"}, {"input", {{"plate", 1}}}}});
    const json responses = json::array({json{{"type", "reasoning"}, {"id", "rs_1"}, {"summary", json::array()}},
                                        json{{"type", "message"}, {"role", "assistant"}, {"content", json::array({json{{"type", "output_text"}, {"text", "Sliced."}}})}},
                                        json{{"type", "function_call"}, {"call_id", "call.1:x"}, {"name", "slice_report"}, {"arguments", "{\"plate\":1}"}}});

    SECTION("conversion keeps text and calls and drops what only one provider can read")
    {
        const json as_responses = assistant_output_as(anthropic, kFormatAnthropic, kFormatResponses);
        REQUIRE(as_responses.size() == 2);
        CHECK(as_responses[0]["content"][0]["text"] == "Opened it.");
        CHECK(as_responses[1]["call_id"] == "toolu_1");
        CHECK(json::parse(as_responses[1]["arguments"].get<std::string>())["plate"] == 1);

        const json as_anthropic = assistant_output_as(responses, kFormatResponses, kFormatAnthropic);
        REQUIRE(as_anthropic.size() == 2);
        CHECK(as_anthropic[0]["text"] == "Sliced.");
        CHECK(as_anthropic[1]["id"] == "call_1_x");
        CHECK(as_anthropic[1]["input"]["plate"] == 1);
    }
    SECTION("an unknown format is a bug, not an empty history")
    {
        CHECK_THROWS_AS(assistant_output_as(anthropic, "unknown", kFormatResponses), std::logic_error);
    }
    SECTION("a Responses request renders an Anthropic turn from the record")
    {
        std::vector<AgentHttpRequest>             requests;
        std::vector<IAgentHttpTransport::EventFn> callbacks;
        AgentServicePtr                           agent = account_agent(kFormatResponses, requests, callbacks);
        AgentRequest                              request;
        JusPrinTest::kept_nowhere(request);
        request.request_id = "turn";
        request.user_text  = "And now?";
        RecordItem assistant{RecordItem::Kind::Assistant};
        assistant.output        = anthropic;
        assistant.output_format = kFormatAnthropic;
        RecordResult result{"toolu_1", "{\"started\":true}", false, std::nullopt};
        RecordItem results{RecordItem::Kind::Results};
        results.results.push_back(result);
        request.record = ChatRecord{kFormatAnthropic, {assistant, results}};
        REQUIRE(agent->start(request));
        REQUIRE(requests.size() == 1);
        const std::string body = requests[0].body;
        CHECK_THAT(body, ContainsSubstring("function_call"));
        CHECK_THAT(body, ContainsSubstring("toolu_1"));
        CHECK_THAT(body, !ContainsSubstring("\"tool_use\""));
        CHECK_THAT(body, !ContainsSubstring("signature"));
    }
    SECTION("an Anthropic request renders a Responses turn from the record")
    {
        std::vector<AgentHttpRequest>             requests;
        std::vector<IAgentHttpTransport::EventFn> callbacks;
        AgentServicePtr                           agent = account_agent(kFormatAnthropic, requests, callbacks);
        AgentRequest                              request;
        JusPrinTest::kept_nowhere(request);
        request.request_id = "turn";
        request.user_text  = "And now?";
        RecordItem assistant{RecordItem::Kind::Assistant};
        assistant.output        = responses;
        assistant.output_format = kFormatResponses;
        RecordResult result{"call.1:x", "{\"ok\":true}", false, std::nullopt};
        RecordItem results{RecordItem::Kind::Results};
        results.results.push_back(result);
        request.record = ChatRecord{kFormatResponses, {assistant, results}};
        REQUIRE(agent->start(request));
        REQUIRE(requests.size() == 1);
        const json body = json::parse(requests[0].body);
        const std::string text = body["messages"].dump();
        CHECK_THAT(text, ContainsSubstring("\"tool_use\""));
        // Both halves of the call use the same safe id.
        CHECK_THAT(text, ContainsSubstring("\"id\":\"call_1_x\""));
        CHECK_THAT(text, ContainsSubstring("\"tool_use_id\":\"call_1_x\""));
        CHECK_THAT(text, !ContainsSubstring("function_call"));
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

TEST_CASE("every gateway reply carries its chat's title, percent-encoded, for Billing and usage", "[agent][account]")
{
    const auto title_header = [](const std::string& chat_key, const std::string& title) {
        std::vector<AgentHttpRequest>             requests;
        std::vector<IAgentHttpTransport::EventFn> callbacks;
        AgentServicePtr                           agent = account_agent(kFormatAnthropic, requests, callbacks);
        AgentRequest                              request;
        JusPrinTest::kept_nowhere(request);
        request.request_id = "turn";
        request.chat_key   = chat_key;
        request.chat_title = title;
        request.user_text  = "hello";
        REQUIRE(agent->start(request));
        REQUIRE(requests.size() == 1);
        return requests[0].header("X-JusPrin-Chat-Title");
    };
    CHECK(title_header("p-1/c-1", "Warping on a 30% PETG panel") == "Warping%20on%20a%2030%25%20PETG%20panel");
    CHECK(title_header("p-1/c-1", "Café ✓") == "Caf%C3%A9%20%E2%9C%93");
    // A chat with no title yet, and a request of no chat, send none.
    CHECK(title_header("p-1/c-1", "").empty());
    CHECK(title_header("", "Orphan").empty());
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
    // A refusal for payment without the gateway's own code is the same want
    // of credits: the Get more credits button goes with its code.
    const AgentError quota = failure(402);
    CHECK(quota.code == "credits_exhausted");
    CHECK(quota.message == "You're out of credits.");
    CHECK_FALSE(quota.retryable);
    // Upstream trouble the gateway reports as its own is the service's.
    CHECK(failure(502).code == "service_unavailable");
}

namespace {

const std::string kAuth = "https://jusprin.test/ent/jusprin/v2/auth/";

// allauth's API answers: a refusal, a code sign-in pending with the session
// token to carry, and a finished sign-in.
std::string refusal(const std::string& code)
{
    return json{{"status", 400}, {"errors", {{{"message", "words"}, {"code", code}}}}}.dump();
}

std::string pending(const std::string& session)
{
    return json{{"status", 401},
                {"data", {{"flows", {{{"id", "login_by_code"}, {"is_pending", true}}}}}},
                {"meta", {{"is_authenticated", false}, {"session_token", session}}}}
        .dump();
}

std::string token_body(const std::string& token)
{
    return json{{"status", 200},
                {"data", {{"user", {{"email", "alex@example.com"}}}}},
                {"meta",
                 {{"is_authenticated", true},
                  {"access_token", token},
                  {"refresh_token", "r"},
                  {"expires_in", 100},
                  {"token_type", "Bearer"},
                  {"scope", "read write"}}}}
        .dump();
}

// Starts an email sign-in: the choice, with nothing sent.
void email_typed(Fixture& f)
{
    REQUIRE(f.sign_in->start_email("alex@example.com"));
    CHECK(f.sign_in->phase() == CloudSignInPhase::Email);
    CHECK(f.sign_in->email_step()->step == CloudEmailStep::Step::Choose);
    CHECK(f.sign_in->email_step()->email == "alex@example.com");
    CHECK_FALSE(f.sign_in->email_step()->busy);
    CHECK(f.machine.post_bodies.empty());
}

// Starts an email sign-in and asks for a code, which the server sends.
void code_sent(Fixture& f, const std::string& session = "s1")
{
    email_typed(f);
    REQUIRE(f.sign_in->send_code());
    CHECK(f.sign_in->email_step()->busy);
    f.machine.answer_post(kAuth + "code/request", 401, pending(session));
    CHECK_FALSE(f.sign_in->poll().has_value());
    CHECK(f.sign_in->email_step()->step == CloudEmailStep::Step::Code);
    CHECK_FALSE(f.sign_in->email_step()->busy);
}

void check_signs_in(Fixture& f, const std::string& token)
{
    CHECK(f.sign_in->phase() == CloudSignInPhase::Checking);
    CHECK(f.machine.tokens == std::vector<std::string>{token, token});
    f.machine.answer(kWho, 200, me_body("alex@example.com"));
    f.machine.answer(kModels, 200, models_body({{"slicer-1", "anthropic"}}));
    const std::optional<CloudSignInOutcome> outcome = f.sign_in->poll();
    REQUIRE(outcome.has_value());
    CHECK(outcome->kind == CloudSignInOutcome::Kind::SignedIn);
    REQUIRE(f.stored.has_value());
    CHECK(f.stored->token == token);
    CHECK(f.stored->email == "alex@example.com");
}

} // namespace

TEST_CASE("an emailed code signs in and is checked like the browser's token", "[agent][account][email]")
{
    Fixture f;
    code_sent(f, "s1");
    CHECK(f.machine.post_bodies.front() == std::make_pair(kAuth + "code/request", json{{"email", "alex@example.com"}}));
    CHECK(f.machine.post_sessions.front().empty());
    CHECK_FALSE(f.sign_in->email_step()->code_resent);
    // The browser is never involved.
    CHECK(f.machine.authorizations.empty());

    REQUIRE(f.sign_in->submit_code("418203"));
    CHECK(f.sign_in->email_step()->busy);
    CHECK_FALSE(f.sign_in->submit_code("418203")); // one at a time
    // The code goes with the session the code request started.
    CHECK(f.machine.post_bodies.back() == std::make_pair(kAuth + "code/confirm", json{{"code", "418203"}}));
    CHECK(f.machine.post_sessions.back() == "s1");
    f.machine.answer_post(kAuth + "code/confirm", 200, token_body("tok-email"));
    CHECK_FALSE(f.sign_in->poll().has_value());
    check_signs_in(f, "tok-email");
}

TEST_CASE("a wrong or spent code stays on the code step and says why", "[agent][account][email]")
{
    Fixture f;
    code_sent(f, "s1");

    REQUIRE(f.sign_in->submit_code("000000"));
    f.machine.answer_post(kAuth + "code/confirm", 400, refusal("incorrect_code"));
    CHECK_FALSE(f.sign_in->poll().has_value());
    CHECK(f.sign_in->phase() == CloudSignInPhase::Email);
    CHECK(f.sign_in->email_step()->refusal == "wrong_code");

    // Expired, or tried too often: allauth has dropped the code.
    REQUIRE(f.sign_in->submit_code("111111"));
    CHECK(f.sign_in->email_step()->refusal.empty()); // cleared while the server checks
    f.machine.answer_post(kAuth + "code/confirm", 409, json{{"status", 409}}.dump());
    CHECK_FALSE(f.sign_in->poll().has_value());
    CHECK(f.sign_in->email_step()->refusal == "expired_code");

    // A new code is a new request, with a new session, and clears the refusal.
    REQUIRE(f.sign_in->send_code());
    CHECK(f.machine.post_bodies.back() == std::make_pair(kAuth + "code/request", json{{"email", "alex@example.com"}}));
    CHECK(f.machine.post_sessions.back().empty());
    f.machine.answer_post(kAuth + "code/request", 401, pending("s2"));
    CHECK_FALSE(f.sign_in->poll().has_value());
    CHECK(f.sign_in->email_step()->refusal.empty());
    CHECK(f.sign_in->email_step()->code_resent);
    REQUIRE(f.sign_in->submit_code("222222"));
    CHECK(f.machine.post_sessions.back() == "s2");
}

TEST_CASE("too many codes for an address is said where the code was asked for", "[agent][account][email]")
{
    Fixture f;
    email_typed(f);
    REQUIRE(f.sign_in->send_code());
    f.machine.answer_post(kAuth + "code/request", 400, refusal("too_many_login_attempts"));
    CHECK_FALSE(f.sign_in->poll().has_value());
    CHECK(f.sign_in->email_step()->step == CloudEmailStep::Step::Choose);
    CHECK(f.sign_in->email_step()->refusal == "too_many_codes");
}

TEST_CASE("a new code refused for too many leaves the pending code working", "[agent][account][email]")
{
    Fixture f;
    code_sent(f, "s1");
    REQUIRE(f.sign_in->send_code());
    f.machine.answer_post(kAuth + "code/request", 400, refusal("too_many_login_attempts"));
    CHECK_FALSE(f.sign_in->poll().has_value());
    CHECK(f.sign_in->email_step()->step == CloudEmailStep::Step::Code);
    CHECK(f.sign_in->email_step()->refusal == "too_many_codes");
    REQUIRE(f.sign_in->submit_code("418203"));
    CHECK(f.machine.post_sessions.back() == "s1");
}

TEST_CASE("the password signs in from the choice", "[agent][account][email]")
{
    Fixture f;
    email_typed(f);
    REQUIRE(f.sign_in->submit_password("wrong"));
    // allauth's login takes the address as the username.
    CHECK(f.machine.post_bodies.back() ==
          std::make_pair(kAuth + "login", json{{"username", "alex@example.com"}, {"password", "wrong"}}));
    CHECK(f.machine.post_sessions.back().empty());
    f.machine.answer_post(kAuth + "login", 400, refusal("username_password_mismatch"));
    CHECK_FALSE(f.sign_in->poll().has_value());
    CHECK(f.sign_in->email_step()->refusal == "wrong_password");
    // A wrong password is not a signed-out account: nothing was stored or erased.
    CHECK_FALSE(f.stored.has_value());
    CHECK(f.erases == 0);

    SECTION("too many tries")
    {
        REQUIRE(f.sign_in->submit_password("wrong"));
        f.machine.answer_post(kAuth + "login", 400, refusal("too_many_login_attempts"));
        CHECK_FALSE(f.sign_in->poll().has_value());
        CHECK(f.sign_in->email_step()->refusal == "too_many_tries");
    }
    SECTION("the right password")
    {
        REQUIRE(f.sign_in->submit_password("correct horse"));
        f.machine.answer_post(kAuth + "login", 200, token_body("tok-pw"));
        CHECK_FALSE(f.sign_in->poll().has_value());
        check_signs_in(f, "tok-pw");
    }
    SECTION("a code instead")
    {
        REQUIRE(f.sign_in->send_code());
        f.machine.answer_post(kAuth + "code/request", 401, pending("s1"));
        CHECK_FALSE(f.sign_in->poll().has_value());
        CHECK(f.sign_in->email_step()->step == CloudEmailStep::Step::Code);
        // Once a code is out, the password is not taken.
        CHECK_FALSE(f.sign_in->submit_password("correct horse"));
    }
}

TEST_CASE("a refusal the person cannot act on ends the email sign-in with one generic error", "[agent][account][email]")
{
    Fixture f;
    code_sent(f);
    REQUIRE(f.sign_in->submit_code("418203"));
    const auto [status, body] =
        GENERATE(std::make_pair(400u, refusal("account_inactive")), std::make_pair(401u, json{{"status", 401}}.dump()),
                 std::make_pair(500u, std::string("<html>")), std::make_pair(0u, std::string()),
                 std::make_pair(200u, std::string("{}")));
    CAPTURE(status, body);
    f.machine.answer_post(kAuth + "code/confirm", status, body);
    const std::optional<CloudSignInOutcome> outcome = f.sign_in->poll();
    REQUIRE(outcome.has_value());
    CHECK(outcome->kind == CloudSignInOutcome::Kind::Failed);
    CHECK(outcome->error.code == "sign_in_failed");
    CHECK(f.sign_in->phase() == CloudSignInPhase::Idle);
    CHECK_FALSE(f.stored.has_value());
}

TEST_CASE("an email sign-in from a release the server no longer serves ends with the server's update words", "[agent][account][email]")
{
    Fixture f;
    code_sent(f);
    REQUIRE(f.sign_in->submit_code("418203"));
    f.machine.answer_post(kAuth + "code/confirm", 426, R"({"type":"error","error":{"code":"update_required","message":"Update to 2.1.0."}})");
    const std::optional<CloudSignInOutcome> outcome = f.sign_in->poll();
    REQUIRE(outcome.has_value());
    CHECK(outcome->kind == CloudSignInOutcome::Kind::Failed);
    CHECK(outcome->error.code == "update_required");
    CHECK(outcome->error.message == "Update to 2.1.0.");
    CHECK(f.sign_in->phase() == CloudSignInPhase::Idle);
    CHECK_FALSE(f.stored.has_value());
}

TEST_CASE("a deactivated account's code request ends with the generic error", "[agent][account][email]")
{
    Fixture f;
    email_typed(f);
    REQUIRE(f.sign_in->send_code());
    f.machine.answer_post(kAuth + "code/request", 400, refusal("account_inactive"));
    const std::optional<CloudSignInOutcome> outcome = f.sign_in->poll();
    REQUIRE(outcome.has_value());
    CHECK(outcome->error.code == "sign_in_failed");
    CHECK_FALSE(f.sign_in->email_step().has_value());
}

TEST_CASE("cancelling an email sign-in drops the server's late answer", "[agent][account][email]")
{
    Fixture f;
    code_sent(f);
    REQUIRE(f.sign_in->submit_code("418203"));
    f.sign_in->cancel();
    CHECK(f.sign_in->phase() == CloudSignInPhase::Idle);
    f.machine.answer_post(kAuth + "code/confirm", 200, token_body("tok-late"));
    CHECK_FALSE(f.sign_in->poll().has_value());
    CHECK(f.sign_in->phase() == CloudSignInPhase::Idle);
    CHECK(f.machine.tokens.empty());
    CHECK_FALSE(f.stored.has_value());
}

TEST_CASE("an email sign-in waits for the browser's to finish, and the other way round", "[agent][account][email]")
{
    Fixture f;
    REQUIRE(f.sign_in->start(CloudBrowserStart::Google));
    CHECK_FALSE(f.sign_in->start_email("alex@example.com"));
    f.sign_in->cancel();
    REQUIRE(f.sign_in->start_email("alex@example.com"));
    CHECK_FALSE(f.sign_in->start(CloudBrowserStart::Google));
    CHECK_FALSE(f.sign_in->start_email(""));
    CHECK_FALSE(f.sign_in->submit_code("418203")); // no code was sent
}
