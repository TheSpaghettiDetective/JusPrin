#pragma once

// Asks a provider for its model list over HTTP. Kept apart from
// ModelCatalog.cpp, which only reads the answer, because this needs Orca's
// Http client and the GUI-free host tests do not link it.

#include "ModelCatalog.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace Slic3r::GUI::JusPrin::Agent {

struct ModelListResult
{
    bool                     ok{false};
    std::vector<ModelOption> models;
    std::string              error; // for the log; the page shows its own words
};

// Fetches every page of the list on Http's own thread and calls `done` there
// once. Setting `*cancelled` stops it; `done` is then not called.
// The signed-in account's credits, plan and usage; nothing when they could not
// be read. `done` runs on the request's thread.
void fetch_account(const ProviderProfile& profile, const std::string& api_key, const std::string& endpoint_override,
                   std::shared_ptr<std::atomic<bool>> cancelled, std::function<void(std::optional<nlohmann::json>)> done);

// The HTML of the box on Preferences › Account, from the account's server at
// `url`, sent with `token` when there is one; nothing when the server sends
// none or cannot be reached. `done` runs on the request's thread.
void fetch_account_box(const ProviderProfile& profile, const std::string& token, const std::string& url,
                       std::shared_ptr<std::atomic<bool>> cancelled, std::function<void(std::optional<std::string>)> done);

void fetch_model_list(const ProviderProfile& profile, const std::string& api_key, const std::string& endpoint_override,
                      std::shared_ptr<std::atomic<bool>> cancelled, std::function<void(ModelListResult)> done);

} // namespace Slic3r::GUI::JusPrin::Agent
