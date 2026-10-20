#pragma once

// Asks a provider for its model list over HTTP. Kept apart from
// ModelCatalog.cpp, which only reads the answer, because this needs Orca's
// Http client and the GUI-free host tests do not link it.

#include "ModelCatalog.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace Slic3r::GUI::JusPrin::Agent {

struct ModelListResult
{
    bool                     ok{false};
    std::vector<ModelOption> models;
    std::string              error; // for the log; the page shows its own words
};

// Fetches every page of the list on Http's own thread and calls `done` there
// once. Setting `*cancelled` stops it; `done` is then not called.
void fetch_model_list(const ProviderProfile& profile, const std::string& api_key, const std::string& endpoint_override,
                      std::shared_ptr<std::atomic<bool>> cancelled, std::function<void(ModelListResult)> done);

} // namespace Slic3r::GUI::JusPrin::Agent
