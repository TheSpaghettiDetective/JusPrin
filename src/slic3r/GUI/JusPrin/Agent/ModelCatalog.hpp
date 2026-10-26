#pragma once

// What a chat may be switched to: each provider's own list of models, read
// from its model-list endpoint, in one shape. GUI-free; the request that
// fetches a list lives in ModelCatalogFetch.cpp, and the host decides when
// to ask.
//
// The three providers describe their models differently:
//   - Anthropic (GET /v1/models) gives each model's thinking types and
//     effort levels; levels are offered only for models with adaptive
//     thinking, where the level is the request's effort.
//   - OpenRouter (GET /api/v1/models) gives a `reasoning` object with the
//     efforts a model accepts, its default, and whether reasoning can be
//     turned off. The list is asked for already filtered to models that
//     take tools, which every Agent turn needs.
//   - OpenAI (GET /v1/models) gives ids only, so the rules below choose the
//     models that can hold a conversation and the levels to offer.
//   - JusPrin's gateway (GET /ent/jusprin/v2/models) describes its aliases in
//     this shape already, with the wire format each is served in for this
//     account; nothing is guessed or filtered.

#include "ProviderProfile.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

struct ModelOption
{
    std::string              id;      // what the request's "model" carries
    std::string              label;   // what the picker shows
    // The thinking levels the model accepts, lowest first ("none" means
    // thinking off); empty when the model has no levels to choose from.
    std::vector<std::string> efforts;
    // The level the provider says the model uses when none is sent; empty
    // when the provider does not say.
    std::string              default_effort;
    // An account provider's alias: the wire format the gateway serves it in
    // for this account (kFormatAnthropic or kFormatResponses). Empty for every
    // other provider, whose row decides the format.
    std::string              format;
};

// Where a provider's list is, beside its request endpoint (an override
// moves both): the last path segment becomes "models", with the query the
// provider needs.
std::string model_list_url(const ProviderProfile& profile, const std::string& endpoint_override);

// One page of a provider's answer, in the provider's order (OpenAI's newest
// first). `next_after` is set when Anthropic says there is another page.
struct ModelListPage
{
    std::vector<ModelOption> models;
    std::string              next_after;
};
ModelListPage parse_model_list(const ProviderProfile& profile, const nlohmann::json& response);

} // namespace Slic3r::GUI::JusPrin::Agent
