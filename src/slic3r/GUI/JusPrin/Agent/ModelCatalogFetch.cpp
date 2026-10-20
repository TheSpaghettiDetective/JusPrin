#include "ModelCatalogFetch.hpp"

#include "slic3r/Utils/Http.hpp"

#include <nlohmann/json.hpp>

#include <utility>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

struct Fetch
{
    ProviderProfile                    profile;
    std::string                        api_key;
    std::string                        url;
    std::shared_ptr<std::atomic<bool>> cancelled;
    std::function<void(ModelListResult)> done;
    ModelListResult                    result;
};

void request_page(std::shared_ptr<Fetch> fetch, const std::string& after)
{
    std::string url = fetch->url;
    if (!after.empty())
        url += (url.find('?') == std::string::npos ? "?" : "&") + std::string("after_id=") + after;
    auto http = Slic3r::Http::get(std::move(url));
    // The same credential and fixed headers a turn sends.
    if (fetch->profile.auth == AuthStyle::Bearer)
        http.header("Authorization", "Bearer " + fetch->api_key);
    else
        http.header("x-api-key", fetch->api_key);
    for (const AgentHttpHeader& header : fetch->profile.headers)
        http.header(header.name, header.value);
    http.header("Accept", "application/json")
        .tls_verify(true)
        .timeout_connect(10)
        .timeout_max(30)
        .on_progress([fetch](Slic3r::Http::Progress, bool& cancel) { cancel = *fetch->cancelled; })
        .on_complete([fetch](std::string body, unsigned status) {
            if (*fetch->cancelled)
                return;
            const nlohmann::json response = nlohmann::json::parse(body, nullptr, false);
            if (status != 200 || response.is_discarded() || !response.contains("data")) {
                fetch->result.error = "model list answered " + std::to_string(status);
                fetch->done(std::move(fetch->result));
                return;
            }
            ModelListPage page = parse_model_list(fetch->profile, response);
            for (ModelOption& option : page.models)
                fetch->result.models.push_back(std::move(option));
            if (!page.next_after.empty()) {
                request_page(fetch, page.next_after);
                return;
            }
            fetch->result.ok = true;
            fetch->done(std::move(fetch->result));
        })
        .on_error([fetch](std::string, std::string error, unsigned status) {
            if (*fetch->cancelled)
                return;
            fetch->result.error = error.empty() ? "model list answered " + std::to_string(status) : error;
            fetch->done(std::move(fetch->result));
        })
        .perform();
}

} // namespace

void fetch_model_list(const ProviderProfile& profile, const std::string& api_key, const std::string& endpoint_override,
                      std::shared_ptr<std::atomic<bool>> cancelled, std::function<void(ModelListResult)> done)
{
    auto fetch       = std::make_shared<Fetch>();
    fetch->profile   = profile;
    fetch->api_key   = api_key;
    fetch->url       = model_list_url(profile, endpoint_override);
    fetch->cancelled = std::move(cancelled);
    fetch->done      = std::move(done);
    request_page(std::move(fetch), {});
}

} // namespace Slic3r::GUI::JusPrin::Agent
