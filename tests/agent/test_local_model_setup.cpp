// Setting up a model on this computer: what the setup asks the app at the
// address, how it tells Ollama from LM Studio, and when a model passes. The
// Ollama answers are as Ollama 0.40.17 gave them on 2026-11-24; the LM Studio
// ones follow LM Studio's REST API documentation (/api/v1/models).

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/LocalModelSetup.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProviderProfile.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Slic3r::GUI::JusPrin::Agent;
using nlohmann::json;

namespace {

struct AppRequest
{
    std::string                                   method;
    std::string                                   url;
    std::string                                   body;
    std::function<void(LocalModelSetup::Answer)> done;
};

struct PostLog
{
    std::vector<AgentHttpRequest>             requests;
    std::vector<IAgentHttpTransport::EventFn> callbacks;
};

class FakeTransport final : public IAgentHttpTransport
{
public:
    explicit FakeTransport(PostLog& log) : m_log(log) {}
    bool post(AgentHttpRequest request, EventFn event) override
    {
        m_log.requests.push_back(std::move(request));
        m_log.callbacks.push_back(std::move(event));
        return true;
    }
    void cancel() override {}

private:
    PostLog& m_log;
};

std::string sse(const json& event) { return "event: message\ndata: " + event.dump() + "\n\n"; }

const std::string kOllamaModels =
    R"({"object":"list","data":[{"id":"qwen3.5:9b","object":"model","created":1795553472,"owned_by":"library"},)"
    R"({"id":"llama3.2:latest","object":"model","created":1729293543,"owned_by":"library"}]})";

std::string ollama_show(const std::vector<std::string>& capabilities)
{
    return json{{"capabilities", capabilities}, {"details", {{"family", "qwen35"}}}}.dump();
}

std::string ollama_ps(const std::string& model, long context)
{
    return json{{"models", json::array({{{"name", model}, {"model", model}, {"size", 9000000000LL}, {"context_length", context}}})}}
        .dump();
}

struct Fixture
{
    std::vector<AppRequest>                 asked;
    PostLog                                 posts;
    std::vector<LocalModelRecord>           stored;
    bool                                    removed{false};
    std::unique_ptr<LocalModelSetup>        setup;

    Fixture()
    {
        LocalModelSetup::Store store;
        store.load   = [this]() -> std::optional<LocalModelRecord> {
            return stored.empty() || removed ? std::nullopt : std::optional<LocalModelRecord>(stored.back());
        };
        store.store  = [this](const LocalModelRecord& record) { stored.push_back(record); };
        store.remove = [this]() { removed = true; };
        setup        = std::make_unique<LocalModelSetup>(
            [this](const std::string& method, const std::string& url, const std::string& body,
                   std::function<void(LocalModelSetup::Answer)> done) { asked.push_back({method, url, body, std::move(done)}); },
            [this]() -> std::unique_ptr<IAgentHttpTransport> { return std::make_unique<FakeTransport>(posts); },
            std::move(store));
    }

    // Answers the latest request the app was asked, then polls once.
    std::optional<LocalSetupOutcome> answer(unsigned status, const std::string& body, const std::string& error = {})
    {
        REQUIRE_FALSE(asked.empty());
        asked.back().done({status, body, error});
        return setup->poll();
    }

    // Finds the two models Ollama has on the computer these answers came from.
    void found_at_ollama(const std::vector<std::string>& qwen = {"completion", "vision", "tools", "thinking"},
                         const std::vector<std::string>& llama = {"completion", "tools"})
    {
        setup->find("http://127.0.0.1:11434");
        CHECK_FALSE(answer(200, kOllamaModels));
        CHECK_FALSE(answer(200, R"({"version":"0.40.17"})"));
        CHECK_FALSE(answer(200, ollama_show(qwen)));
        const std::optional<LocalSetupOutcome> outcome = answer(200, ollama_show(llama));
        REQUIRE(outcome);
        REQUIRE(outcome->kind == LocalSetupOutcome::Kind::Models);
    }

    // The probe answers, as the model would with any reply.
    std::optional<LocalSetupOutcome> probe_answers()
    {
        REQUIRE_FALSE(posts.callbacks.empty());
        posts.callbacks.back()({AgentHttpEvent::Kind::Data, sse({{"type", "response.output_text.delta"}, {"delta", "ready"}}), {}, 0});
        posts.callbacks.back()(
            {AgentHttpEvent::Kind::Data, sse({{"type", "response.completed"}, {"response", {{"output", json::array()}}}}), {}, 0});
        posts.callbacks.back()({AgentHttpEvent::Kind::Complete, {}, {}, 200});
        return setup->poll();
    }
};

} // namespace

TEST_CASE("finding at Ollama lists its chat models with what each can do", "[LocalModelSetup]")
{
    Fixture fixture;
    fixture.setup->find("127.0.0.1:11434/");
    CHECK(fixture.setup->busy());
    REQUIRE(fixture.asked.size() == 1);
    CHECK(fixture.asked[0].method == "GET");
    CHECK(fixture.asked[0].url == "http://127.0.0.1:11434/v1/models");

    CHECK_FALSE(fixture.answer(200, kOllamaModels));
    CHECK(fixture.asked.back().url == "http://127.0.0.1:11434/api/version");
    CHECK_FALSE(fixture.answer(200, R"({"version":"0.40.17"})"));
    CHECK(fixture.asked.back().method == "POST");
    CHECK(fixture.asked.back().url == "http://127.0.0.1:11434/api/show");
    CHECK(json::parse(fixture.asked.back().body) == json{{"model", "qwen3.5:9b"}});
    CHECK_FALSE(fixture.answer(200, ollama_show({"completion", "vision", "tools", "thinking"})));
    CHECK(json::parse(fixture.asked.back().body) == json{{"model", "llama3.2:latest"}});
    const std::optional<LocalSetupOutcome> outcome = fixture.answer(200, ollama_show({"completion", "tools"}));

    REQUIRE(outcome);
    REQUIRE(outcome->kind == LocalSetupOutcome::Kind::Models);
    CHECK_FALSE(fixture.setup->busy());
    CHECK(outcome->models.address == "http://127.0.0.1:11434");
    CHECK(outcome->models.runtime == "Ollama");
    REQUIRE(outcome->models.models.size() == 2);
    CHECK(outcome->models.models[0].id == "qwen3.5:9b");
    CHECK(outcome->models.models[0].tools == true);
    CHECK(outcome->models.models[0].images == true);
    CHECK(outcome->models.models[1].id == "llama3.2:latest");
    CHECK(outcome->models.models[1].images == false);
}

TEST_CASE("an embedding model at Ollama is no chat's model and is left out", "[LocalModelSetup]")
{
    Fixture fixture;
    fixture.setup->find("http://127.0.0.1:11434");
    CHECK_FALSE(fixture.answer(200, R"({"object":"list","data":[{"id":"nomic-embed-text:latest"},{"id":"qwen3.5:9b"}]})"));
    CHECK_FALSE(fixture.answer(200, R"({"version":"0.40.17"})"));
    CHECK_FALSE(fixture.answer(200, ollama_show({"embedding"})));
    const std::optional<LocalSetupOutcome> outcome = fixture.answer(200, ollama_show({"completion", "tools"}));
    REQUIRE(outcome);
    REQUIRE(outcome->models.models.size() == 1);
    CHECK(outcome->models.models[0].id == "qwen3.5:9b");
}

TEST_CASE("nothing answering at the address is the error the person can act on, with their address", "[LocalModelSetup]")
{
    Fixture fixture;
    fixture.setup->find("localhost:1234");
    const std::optional<LocalSetupOutcome> outcome = fixture.answer(0, {}, "Couldn't connect to server");
    REQUIRE(outcome);
    REQUIRE(outcome->kind == LocalSetupOutcome::Kind::Failed);
    CHECK(outcome->error.code == "local_unreachable");
    CHECK_THAT(outcome->error.message, Catch::Matchers::ContainsSubstring("localhost:1234"));
    CHECK_THAT(outcome->error.message, Catch::Matchers::ContainsSubstring("Ollama or LM Studio"));
    CHECK_FALSE(fixture.setup->busy());
}

TEST_CASE("an app that answers its models but not Ollama's version is asked as LM Studio", "[LocalModelSetup]")
{
    Fixture fixture;
    fixture.setup->find("http://127.0.0.1:1234");
    CHECK_FALSE(fixture.answer(200, R"({"object":"list","data":[{"id":"qwen/qwen3-4b"},{"id":"google/gemma-3-4b"},)"
                                    R"({"id":"text-embedding-nomic-embed-text-v1.5"}]})"));
    CHECK_FALSE(fixture.answer(404, R"j({"error":"Unexpected endpoint or method. (GET /api/version)"})j"));
    CHECK(fixture.asked.back().url == "http://127.0.0.1:1234/api/v1/models");
    const json lms = {{"models", json::array({
        {{"type", "llm"}, {"key", "qwen/qwen3-4b"}, {"capabilities", {{"vision", false}, {"trained_for_tool_use", true}}},
         {"loaded_instances", json::array()}},
        {{"type", "llm"}, {"key", "google/gemma-3-4b"}, {"capabilities", {{"vision", true}, {"trained_for_tool_use", false}}},
         {"loaded_instances", json::array()}},
        {{"type", "embedding"}, {"key", "text-embedding-nomic-embed-text-v1.5"}, {"loaded_instances", json::array()}},
    })}};
    const std::optional<LocalSetupOutcome> outcome = fixture.answer(200, lms.dump());
    REQUIRE(outcome);
    REQUIRE(outcome->kind == LocalSetupOutcome::Kind::Models);
    CHECK(outcome->models.runtime == "LM Studio");
    REQUIRE(outcome->models.models.size() == 2);
    CHECK(outcome->models.models[0].tools == true);
    CHECK(outcome->models.models[0].images == false);
    CHECK(outcome->models.models[1].tools == false);
    CHECK(outcome->models.models[1].images == true);
}

TEST_CASE("an app that is neither lists its models with nothing known about them", "[LocalModelSetup]")
{
    Fixture fixture;
    fixture.setup->find("http://127.0.0.1:8080");
    CHECK_FALSE(fixture.answer(200, R"({"object":"list","data":[{"id":"some-model"}]})"));
    CHECK_FALSE(fixture.answer(404, "Not Found"));
    const std::optional<LocalSetupOutcome> outcome = fixture.answer(404, "Not Found");
    REQUIRE(outcome);
    CHECK(outcome->models.runtime.empty());
    REQUIRE(outcome->models.models.size() == 1);
    CHECK_FALSE(outcome->models.models[0].tools.has_value());
    CHECK_FALSE(outcome->models.models[0].images.has_value());
}

TEST_CASE("a model that answers with the app's tools and a 32k context passes and is stored", "[LocalModelSetup]")
{
    Fixture fixture;
    fixture.found_at_ollama();
    fixture.setup->check("qwen3.5:9b");
    CHECK(fixture.setup->busy());
    CHECK_FALSE(fixture.setup->poll());

    // The check is a real turn's request, at the address, with no credential.
    REQUIRE(fixture.posts.requests.size() == 1);
    const AgentHttpRequest& probe = fixture.posts.requests[0];
    CHECK(probe.url == "http://127.0.0.1:11434/v1/responses");
    CHECK(probe.header("Authorization").empty());
    CHECK(probe.request_timeout_seconds == 0);
    const json body = json::parse(probe.body);
    CHECK(body.at("model") == "qwen3.5:9b");
    CHECK_FALSE(body.at("tools").empty());
    // A model on this computer has no web search.
    for (const json& tool : body.at("tools"))
        CHECK(tool.value("type", "") != "web_search");

    CHECK_FALSE(fixture.probe_answers());
    CHECK(fixture.asked.back().url == "http://127.0.0.1:11434/api/ps");
    const std::optional<LocalSetupOutcome> outcome = fixture.answer(200, ollama_ps("qwen3.5:9b", 32768));
    REQUIRE(outcome);
    REQUIRE(outcome->kind == LocalSetupOutcome::Kind::Checked);
    CHECK(outcome->service != nullptr);
    CHECK_FALSE(fixture.setup->busy());
    REQUIRE(fixture.stored.size() == 1);
    CHECK(fixture.stored[0] == LocalModelRecord{"http://127.0.0.1:11434", "qwen3.5:9b", "Ollama", true});
}

TEST_CASE("a model loaded with a context under 32k can't run the app's tools and is not stored", "[LocalModelSetup]")
{
    Fixture fixture;
    fixture.found_at_ollama();
    fixture.setup->check("llama3.2:latest");
    CHECK_FALSE(fixture.setup->poll());
    CHECK_FALSE(fixture.probe_answers());
    const std::optional<LocalSetupOutcome> outcome = fixture.answer(200, ollama_ps("llama3.2:latest", 4096));
    REQUIRE(outcome);
    REQUIRE(outcome->kind == LocalSetupOutcome::Kind::Failed);
    CHECK(outcome->error.code == "local_cant_run");
    CHECK(fixture.stored.empty());
}

TEST_CASE("a model the app says takes no tools fails at once, with nothing sent to it", "[LocalModelSetup]")
{
    Fixture fixture;
    fixture.found_at_ollama({"completion", "vision"});
    fixture.setup->check("qwen3.5:9b");
    const std::optional<LocalSetupOutcome> outcome = fixture.setup->poll();
    REQUIRE(outcome);
    CHECK(outcome->error.code == "local_cant_run");
    CHECK(fixture.posts.requests.empty());
    CHECK(fixture.stored.empty());
}

TEST_CASE("the app stopping during the check is the unreachable error", "[LocalModelSetup]")
{
    Fixture fixture;
    fixture.found_at_ollama();
    fixture.setup->check("qwen3.5:9b");
    CHECK_FALSE(fixture.setup->poll());
    fixture.posts.callbacks.back()({AgentHttpEvent::Kind::Error, {}, "Couldn't connect to server", 0});
    const std::optional<LocalSetupOutcome> outcome = fixture.setup->poll();
    REQUIRE(outcome);
    REQUIRE(outcome->kind == LocalSetupOutcome::Kind::Failed);
    CHECK(outcome->error.code == "local_unreachable");
    CHECK_THAT(outcome->error.message, Catch::Matchers::ContainsSubstring("127.0.0.1:11434"));
}

TEST_CASE("an answer that names nothing the person can act on stops the setup and goes up", "[LocalModelSetup]")
{
    Fixture fixture;
    fixture.setup->find("http://127.0.0.1:11434");
    fixture.asked.back().done({500, R"({"error":"llama runner process has terminated"})", {}});
    CHECK_THROWS_WITH(fixture.setup->poll(), Catch::Matchers::ContainsSubstring("llama runner process has terminated"));
    CHECK_FALSE(fixture.setup->busy());
}

TEST_CASE("an answer that comes after Cancel is dropped", "[LocalModelSetup]")
{
    Fixture fixture;
    fixture.setup->find("http://127.0.0.1:11434");
    fixture.setup->cancel();
    CHECK_FALSE(fixture.setup->busy());
    fixture.asked.back().done({200, kOllamaModels, {}});
    CHECK_FALSE(fixture.setup->poll());
}

TEST_CASE("checking a model the last list does not have is a broken page", "[LocalModelSetup]")
{
    Fixture fixture;
    fixture.found_at_ollama();
    CHECK_THROWS(fixture.setup->check("mistral:7b"));
}

TEST_CASE("an address is kept as the app's base, whatever the person typed", "[LocalModelSetup]")
{
    CHECK(normalized_local_address(" 127.0.0.1:11434 ") == "http://127.0.0.1:11434");
    CHECK(normalized_local_address("http://localhost:1234/v1/") == "http://localhost:1234");
    CHECK(local_endpoint("http://localhost:1234") == "http://localhost:1234/v1/responses");
    CHECK(local_address_shown("http://127.0.0.1:11434/v1/responses") == "127.0.0.1:11434");
}
