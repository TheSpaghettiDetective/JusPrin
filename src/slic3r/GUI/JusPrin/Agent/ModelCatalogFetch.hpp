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
    bool                     refused{false}; // answered 401 or 403
    // The server's words when it no longer serves this release (426).
    std::string              update_message;
    std::string              error; // for the log; the page shows its own words
};

// Fetches every page of the list on Http's own thread and calls `done` there
// once. Setting `*cancelled` stops it; `done` is then not called.
// The signed-in account's credits, plan and usage; nothing when they could not
// be read, or were not account_reply_readable. `done` runs on the request's
// thread.
void fetch_account(const ProviderProfile& profile, const std::string& api_key, const std::string& endpoint_override,
                   std::shared_ptr<std::atomic<bool>> cancelled, std::function<void(std::optional<nlohmann::json>)> done);

// The HTML of the box on Preferences › Account, from the account's server at
// `url`, sent with `token` when there is one; nothing when the server sends
// none or cannot be reached. `done` runs on the request's thread.
void fetch_account_box(const ProviderProfile& profile, const std::string& token, const std::string& url,
                       std::shared_ptr<std::atomic<bool>> cancelled, std::function<void(std::optional<std::string>)> done);

// Deletes the account `token` signs in as, with DELETE at `url`; `done` says
// whether the server answered that it did (204), on the request's thread.
void delete_account(const ProviderProfile& profile, const std::string& token, const std::string& url,
                    std::shared_ptr<std::atomic<bool>> cancelled, std::function<void(bool)> done);

void fetch_model_list(const ProviderProfile& profile, const std::string& api_key, const std::string& endpoint_override,
                      std::shared_ptr<std::atomic<bool>> cancelled, std::function<void(ModelListResult)> done);

} // namespace Slic3r::GUI::JusPrin::Agent
