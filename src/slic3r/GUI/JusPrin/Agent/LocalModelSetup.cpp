#include "LocalModelSetup.hpp"

#include "AgentSetup.hpp"
#include "ProviderAgent.hpp"
#include "ProviderFactory.hpp"
#include "ProviderProfile.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

using json = nlohmann::json;

json parsed(const std::string& body) { return json::parse(body, nullptr, false); }

bool has(const json& list, const char* item)
{
    return list.is_array() && std::find(list.begin(), list.end(), item) != list.end();
}

// What the app answered, for the report of an answer that names nothing the
// person can act on.
std::string said(const LocalModelSetup::Answer& answer)
{
    return "answered " + std::to_string(answer.status) + (answer.error.empty() ? "" : " (" + answer.error + ")") + ": " +
           answer.body.substr(0, 2048);
}

} // namespace

LocalModelSetup::LocalModelSetup(Fetch fetch, TransportFactory transport, Store store, Clock clock)
    : m_fetch(std::move(fetch)), m_transport(std::move(transport)), m_store(std::move(store)), m_clock(std::move(clock))
{
    if (!m_clock)
        m_clock = []() { return std::chrono::steady_clock::now(); };
}

LocalModelSetup::~LocalModelSetup() { stop(); }

void LocalModelSetup::find(const std::string& address)
{
    if (busy())
        throw std::logic_error("jusprin-local-setup: finding models while setup runs");
    m_list         = LocalModelList{};
    m_list.address = normalized_local_address(address);
    if (m_list.address.empty())
        throw std::runtime_error("jusprin-local-setup: finding models at an empty address");
    m_started = m_clock();
    m_step    = Step::Models;
    request("GET", "/v1/models");
}

void LocalModelSetup::check(const std::string& model)
{
    if (busy())
        throw std::logic_error("jusprin-local-setup: checking a model while setup runs");
    const auto listed = std::find_if(m_list.models.begin(), m_list.models.end(),
                                     [&](const LocalModelInfo& info) { return info.id == model; });
    // The page offers the last list's models only.
    if (listed == m_list.models.end())
        throw std::runtime_error("jusprin-local-setup: checking " + model + ", which the last list at " + m_list.address +
                                 " does not have");
    m_checking = model;
    m_started  = m_clock();
    m_step     = Step::Probe;
}

void LocalModelSetup::cancel() { stop(); }

void LocalModelSetup::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_answers->mutex);
        ++m_answers->generation;
        m_answers->queue.clear();
    }
    if (m_candidate)
        m_candidate->cancel();
    m_candidate.reset();
    m_step = Step::Idle;
}

void LocalModelSetup::request(const std::string& method, const std::string& path, const std::string& body)
{
    std::uint64_t generation;
    {
        std::lock_guard<std::mutex> lock(m_answers->mutex);
        generation = m_answers->generation;
    }
    m_fetch(method, m_list.address + path, body, [answers = m_answers, generation](Answer answer) {
        std::lock_guard<std::mutex> lock(answers->mutex);
        if (generation == answers->generation)
            answers->queue.push_back(std::move(answer));
    });
}

std::optional<LocalSetupOutcome> LocalModelSetup::poll()
{
    if (m_step == Step::Probe)
        return poll_probe();
    while (busy()) {
        Answer answer;
        {
            std::lock_guard<std::mutex> lock(m_answers->mutex);
            if (m_answers->queue.empty())
                return std::nullopt;
            answer = std::move(m_answers->queue.front());
            m_answers->queue.pop_front();
        }
        if (std::optional<LocalSetupOutcome> outcome = advance(answer))
            return outcome;
        if (m_step == Step::Probe)
            return poll_probe();
    }
    return std::nullopt;
}

std::optional<LocalSetupOutcome> LocalModelSetup::advance(const Answer& answer)
{
    const json body = parsed(answer.body);
    switch (m_step) {
    case Step::Models: {
        // Nothing answered: the app that runs the model is not running, or
        // the address is wrong. Either way the person starts it or fixes it.
        if (answer.status == 0)
            return failed(local_unreachable(m_list.address));
        if (answer.status != 200 || !body.is_object() || !body.contains("data") || !body["data"].is_array())
            unexpected("listing its models", answer);
        for (const json& entry : body["data"])
            if (entry.is_object() && entry.contains("id") && entry["id"].is_string())
                m_list.models.push_back({entry["id"].get<std::string>(), std::nullopt, std::nullopt});
        m_step = Step::Version;
        request("GET", "/api/version");
        return std::nullopt;
    }
    case Step::Version:
        // Ollama answers its own version; LM Studio and others do not.
        if (answer.status == 200 && body.is_object() && body.contains("version")) {
            m_list.runtime = kRuntimeOllama;
            m_showing      = 0;
            return next_show();
        }
        m_step = Step::LmsModels;
        request("GET", "/api/v1/models");
        return std::nullopt;
    case Step::Show: {
        if (answer.status != 200 || !body.is_object())
            unexpected("describing " + m_list.models[m_showing].id, answer);
        const json capabilities = body.value("capabilities", json::array());
        LocalModelInfo& info = m_list.models[m_showing];
        // An embedding model writes no replies; it is no chat's model.
        if (!has(capabilities, "completion")) {
            m_list.models.erase(m_list.models.begin() + static_cast<std::ptrdiff_t>(m_showing));
        } else {
            info.tools  = has(capabilities, "tools");
            info.images = has(capabilities, "vision");
            ++m_showing;
        }
        return next_show();
    }
    case Step::LmsModels: {
        // An app that is not LM Studio either: what its models can do is
        // unknown, and the check finds out whether they run.
        if (answer.status != 200 || !body.is_object() || !body.contains("models") || !body["models"].is_array()) {
            m_step = Step::Idle;
            LocalSetupOutcome outcome;
            outcome.kind       = LocalSetupOutcome::Kind::Models;
            outcome.models     = m_list;
            outcome.elapsed_ms = elapsed_ms();
            return outcome;
        }
        m_list.runtime = kRuntimeLmStudio;
        std::vector<LocalModelInfo> kept;
        for (LocalModelInfo info : m_list.models) {
            const auto entry = std::find_if(body["models"].begin(), body["models"].end(), [&](const json& model) {
                return model.is_object() && model.value("key", "") == info.id;
            });
            if (entry != body["models"].end()) {
                if (entry->value("type", "") == "embedding")
                    continue;
                const json capabilities = entry->value("capabilities", json::object());
                if (capabilities.is_object()) {
                    if (capabilities.contains("trained_for_tool_use") && capabilities["trained_for_tool_use"].is_boolean())
                        info.tools = capabilities["trained_for_tool_use"].get<bool>();
                    if (capabilities.contains("vision") && capabilities["vision"].is_boolean())
                        info.images = capabilities["vision"].get<bool>();
                }
            }
            kept.push_back(std::move(info));
        }
        m_list.models = std::move(kept);
        m_step        = Step::Idle;
        LocalSetupOutcome outcome;
        outcome.kind       = LocalSetupOutcome::Kind::Models;
        outcome.models     = m_list;
        outcome.elapsed_ms = elapsed_ms();
        return outcome;
    }
    case Step::LoadedContext: {
        if (answer.status != 200 || !body.is_object())
            unexpected("saying how much context " + m_checking + " was loaded with", answer);
        std::optional<long> context;
        if (m_list.runtime == kRuntimeOllama) {
            for (const json& loaded : body.value("models", json::array()))
                if (loaded.is_object() && (loaded.value("name", "") == m_checking || loaded.value("model", "") == m_checking) &&
                    loaded.contains("context_length") && loaded["context_length"].is_number_integer())
                    context = loaded["context_length"].get<long>();
        } else {
            for (const json& model : body.value("models", json::array()))
                if (model.is_object() && model.value("key", "") == m_checking)
                    for (const json& instance : model.value("loaded_instances", json::array()))
                        if (instance.is_object() && instance.contains("config") && instance["config"].is_object() &&
                            instance["config"].contains("context_length") &&
                            instance["config"]["context_length"].is_number_integer())
                            context = instance["config"]["context_length"].get<long>();
        }
        if (!context)
            unexpected("saying how much context " + m_checking + " was loaded with", answer);
        if (*context < kLocalMinContext)
            return failed(local_cant_run());
        return checked();
    }
    case Step::Idle:
    case Step::Probe:
        break;
    }
    throw std::logic_error("jusprin-local-setup: an answer with no request waiting for it");
}

std::optional<LocalSetupOutcome> LocalModelSetup::next_show()
{
    if (m_showing < m_list.models.size()) {
        m_step = Step::Show;
        request("POST", "/api/show", json{{"model", m_list.models[m_showing].id}}.dump());
        return std::nullopt;
    }
    m_step = Step::Idle;
    LocalSetupOutcome outcome;
    outcome.kind       = LocalSetupOutcome::Kind::Models;
    outcome.models     = m_list;
    outcome.elapsed_ms = elapsed_ms();
    return outcome;
}

std::optional<LocalSetupOutcome> LocalModelSetup::start_probe()
{
    const auto listed = std::find_if(m_list.models.begin(), m_list.models.end(),
                                     [&](const LocalModelInfo& info) { return info.id == m_checking; });
    // The app says the model takes no tools: the check would only fail.
    if (listed->tools == false)
        return failed(local_cant_run());
    m_candidate = make_provider_agent(*profile_for(kLocalProvider), {}, m_checking, local_endpoint(m_list.address), m_transport());
    if (!m_candidate->start(setup_probe_request()))
        throw std::logic_error("jusprin-local-setup: the check's agent would not start");
    return std::nullopt;
}

std::optional<LocalSetupOutcome> LocalModelSetup::poll_probe()
{
    if (!m_candidate)
        if (std::optional<LocalSetupOutcome> refused = start_probe())
            return refused;
    while (const std::optional<AgentEvent> event = m_candidate->poll()) {
        switch (event->kind) {
        case AgentEventKind::TextDelta:
            break; // What it says is irrelevant; that it answers is the test.
        case AgentEventKind::ToolCall:
        case AgentEventKind::Completed:
            // It answered with the app's tools offered. An app that cannot
            // say how much context it loaded the model with is not asked.
            m_candidate->cancel();
            if (m_list.runtime.empty())
                return checked();
            m_step = Step::LoadedContext;
            request("GET", m_list.runtime == kRuntimeOllama ? "/api/ps" : "/api/v1/models");
            return std::nullopt;
        case AgentEventKind::Failed:
            if (!event->error)
                throw std::logic_error("jusprin-local-setup: the check failed with no error");
            return failed(*event->error);
        }
    }
    return std::nullopt;
}

LocalSetupOutcome LocalModelSetup::failed(AgentError error)
{
    LocalSetupOutcome outcome;
    outcome.kind       = LocalSetupOutcome::Kind::Failed;
    outcome.error      = std::move(error);
    outcome.elapsed_ms = elapsed_ms();
    stop();
    return outcome;
}

LocalSetupOutcome LocalModelSetup::checked()
{
    const auto listed = std::find_if(m_list.models.begin(), m_list.models.end(),
                                     [&](const LocalModelInfo& info) { return info.id == m_checking; });
    LocalSetupOutcome outcome;
    outcome.kind = LocalSetupOutcome::Kind::Checked;
    // A model the app says nothing about gets no pictures: sending one to a
    // model that cannot read it fails the turn.
    outcome.record     = {m_list.address, m_checking, m_list.runtime, listed->images.value_or(false)};
    outcome.elapsed_ms = elapsed_ms();
    m_store.store(outcome.record);
    {
        std::lock_guard<std::mutex> lock(m_answers->mutex);
        ++m_answers->generation;
        m_answers->queue.clear();
    }
    outcome.service = std::move(m_candidate);
    m_candidate.reset();
    m_step = Step::Idle;
    return outcome;
}

void LocalModelSetup::unexpected(const std::string& what, const Answer& answer)
{
    const std::string runtime = m_list.runtime.empty() ? std::string("the app") : m_list.runtime;
    const std::string address = m_list.address;
    stop();
    throw std::runtime_error("jusprin-local-setup: " + runtime + " at " + address + ", " + what + ", " + said(answer));
}

int LocalModelSetup::elapsed_ms() const
{
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(m_clock() - m_started).count());
}

std::optional<LocalModelRecord> LocalModelSetup::saved() const
{
    return m_store.load ? m_store.load() : std::nullopt;
}

void LocalModelSetup::remove()
{
    if (busy())
        throw std::logic_error("jusprin-local-setup: removing the model while setup runs");
    if (!m_store.remove)
        throw std::logic_error("jusprin-local-setup: removing from a setup with no store");
    m_store.remove();
}

} // namespace Slic3r::GUI::JusPrin::Agent
