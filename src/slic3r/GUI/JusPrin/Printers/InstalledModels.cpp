#include "InstalledModels.hpp"

namespace Slic3r::GUI::JusPrin::Printers {

namespace {

bool enabled(const VendorMap& vendors, const std::string& vendor, const std::string& model)
{
    const auto models = vendors.find(vendor);
    if (models == vendors.end())
        return false;
    const auto variants = models->second.find(model);
    return variants != models->second.end() && !variants->second.empty();
}

} // namespace

std::vector<InstalledModel> newly_installed_models(const VendorMap& before, const VendorMap& after,
                                                   const std::string& selected_model,
                                                   const std::string& selected_variant)
{
    std::vector<InstalledModel> installed;
    for (const auto& [vendor, models] : after) {
        for (const auto& [model, variants] : models) {
            if (variants.empty() || enabled(before, vendor, model))
                continue;
            std::string variant = *variants.begin();
            if (model == selected_model && variants.count(selected_variant) > 0)
                variant = selected_variant;
            else if (variants.count("0.4") > 0)
                variant = "0.4";
            installed.push_back({vendor, model, variant});
        }
    }
    return installed;
}

} // namespace Slic3r::GUI::JusPrin::Printers
