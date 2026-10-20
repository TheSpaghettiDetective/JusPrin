// Which provider rows exist, and what the Agent's stored choices make of
// them: the part of AgentConfiguration.cpp that does not touch AppConfig.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/ProviderFactory.hpp"

#include <cstdlib>
#include <set>

using namespace Slic3r::GUI::JusPrin::Agent;

namespace {

class NullTransport final : public IAgentHttpTransport
{
public:
    bool post(AgentHttpRequest, EventFn) override { return true; }
    void cancel() override {}
};

std::unique_ptr<IAgentHttpTransport> null_transport() { return std::make_unique<NullTransport>(); }

AgentSettings configured(std::string provider, std::string key = "sk-stored")
{
    AgentSettings settings;
    settings.enabled       = true;
    settings.cloud_consent = true;
    settings.provider      = std::move(provider);
    settings.api_key       = std::move(key);
    return settings;
}

// Sets an environment variable for one scope and restores what was there.
class ScopedEnvironment
{
public:
    ScopedEnvironment(const char* name, const char* value) : m_name(name)
    {
        if (const char* old = std::getenv(name)) {
            m_had = true;
            m_old = old;
        }
        set(value);
    }
    ~ScopedEnvironment() { set(m_had ? m_old.c_str() : nullptr); }

private:
    void set(const char* value)
    {
#ifdef _WIN32
        _putenv_s(m_name, value == nullptr ? "" : value);
#else
        if (value == nullptr)
            unsetenv(m_name);
        else
            setenv(m_name, value, 1);
#endif
    }
    const char* m_name;
    bool        m_had{false};
    std::string m_old;
};

} // namespace

TEST_CASE("every provider row is complete and has an id of its own", "[agent][providers]")
{
    std::set<std::string> ids;
    for (const ProviderProfile& profile : provider_profiles()) {
        CAPTURE(profile.id);
        CHECK(ids.insert(profile.id).second);
        CHECK_FALSE(profile.endpoint.empty());
        CHECK_FALSE(profile.model.empty());
        CHECK_FALSE(profile.display_name.empty());
        CHECK(profile_for(profile.id) == &profile);
    }
    CHECK(ids == std::set<std::string>{"anthropic", "openai", "openrouter"});
    CHECK(profile_for("other") == nullptr);
    CHECK(&openai_profile() == profile_for("openai"));
}

TEST_CASE("a stored OpenRouter choice loads a ready OpenRouter agent", "[agent][providers][openrouter]")
{
    const AgentRuntime runtime = resolve_agent_runtime(configured("openrouter"), "", &null_transport);
    CHECK(runtime.provider == "openrouter");
    CHECK(runtime.availability == AgentAvailability::Ready);
    REQUIRE(runtime.service != nullptr);
    CHECK(runtime.service->ready());
    CHECK(runtime.unavailable_reason.empty());
}

TEST_CASE("the stored choices say why there is no agent, in the provider's own name", "[agent][providers]")
{
    SECTION("no provider stored means OpenAI, as before")
    {
        const AgentRuntime runtime = resolve_agent_runtime(configured(""), "", &null_transport);
        CHECK(runtime.provider == "openai");
        CHECK(runtime.availability == AgentAvailability::Ready);
    }
    SECTION("an OpenRouter choice without its key")
    {
        const AgentRuntime runtime = resolve_agent_runtime(configured("openrouter", ""), "", &null_transport);
        CHECK(runtime.service == nullptr);
        CHECK(runtime.unavailable_reason == "No OpenRouter API key is configured.");
    }
    SECTION("an OpenAI choice without its key")
    {
        const AgentRuntime runtime = resolve_agent_runtime(configured("openai", ""), "", &null_transport);
        CHECK(runtime.unavailable_reason == "No OpenAI API key is configured.");
    }
    SECTION("a provider this build has no row for")
    {
        const AgentRuntime runtime = resolve_agent_runtime(configured("other"), "", &null_transport);
        CHECK(runtime.service == nullptr);
        CHECK(runtime.unavailable_reason == "The configured Agent provider is not supported.");
    }
    SECTION("not enabled, or no consent")
    {
        AgentSettings settings = configured("openrouter");
        settings.cloud_consent = false;
        CHECK(resolve_agent_runtime(settings, "", &null_transport).unavailable_reason ==
              "Cloud Agent access requires explicit consent.");
        settings.enabled = false;
        CHECK(resolve_agent_runtime(settings, "", &null_transport).unavailable_reason == "The Agent is not enabled.");
    }
}

TEST_CASE("JUSPRIN_AGENT_ENDPOINT redirects the Agent, and JUSPRIN_OPENAI_ENDPOINT still does", "[agent][providers]")
{
    {
        ScopedEnvironment agent("JUSPRIN_AGENT_ENDPOINT", nullptr);
        ScopedEnvironment openai("JUSPRIN_OPENAI_ENDPOINT", nullptr);
        CHECK(agent_endpoint_override().empty());
    }
    {
        ScopedEnvironment agent("JUSPRIN_AGENT_ENDPOINT", nullptr);
        ScopedEnvironment openai("JUSPRIN_OPENAI_ENDPOINT", "http://127.0.0.1:1/v1/responses");
        CHECK(agent_endpoint_override() == "http://127.0.0.1:1/v1/responses");
    }
    {
        ScopedEnvironment agent("JUSPRIN_AGENT_ENDPOINT", "http://127.0.0.1:2/v1/responses");
        ScopedEnvironment openai("JUSPRIN_OPENAI_ENDPOINT", "http://127.0.0.1:1/v1/responses");
        CHECK(agent_endpoint_override() == "http://127.0.0.1:2/v1/responses");
    }

    // The override reaches the agent of whichever provider is stored.
    struct Capture final : IAgentHttpTransport
    {
        explicit Capture(std::string& url) : url(url) {}
        bool post(AgentHttpRequest request, EventFn) override
        {
            url = request.url;
            return true;
        }
        void cancel() override {}
        std::string& url;
    };
    std::string  url;
    AgentRuntime runtime = resolve_agent_runtime(configured("openrouter"), "http://127.0.0.1:3/v1/responses",
                                                 [&url]() -> std::unique_ptr<IAgentHttpTransport> { return std::make_unique<Capture>(url); });
    AgentRequest request;
    request.request_id = "r";
    request.user_text  = "hi";
    REQUIRE(runtime.service->start(request));
    CHECK(url == "http://127.0.0.1:3/v1/responses");
}
