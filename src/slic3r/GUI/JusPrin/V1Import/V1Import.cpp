#include "V1Import.hpp"

#include "../Support/AppFolders.hpp"

#include <boost/log/trivial.hpp>

namespace Slic3r::GUI::JusPrin::V1Import {

namespace fs = std::filesystem;

namespace {

// JusPrin 1's app key, which names its folder and its settings file.
constexpr const char* kV1Key = "JusPrin";
constexpr const char* kNetworkFile = "BambuNetworkEngine.conf";

} // namespace

fs::path v1_folder() { return default_app_folder(kV1Key); }

bool copy_network_settings(const fs::path& v1, const fs::path& data_dir)
{
    std::error_code error;
    if (!fs::is_regular_file(v1 / kNetworkFile, error) || fs::exists(data_dir / kNetworkFile, error))
        return false;
    fs::copy_file(v1 / kNetworkFile, data_dir / kNetworkFile, error);
    // The printers and presets came over without it; the plug-in starts
    // with no saved settings, as on a fresh install.
    if (error)
        BOOST_LOG_TRIVIAL(error) << "Copying " << kNetworkFile << " from JusPrin 1 failed: " << error.message();
    return !error;
}

} // namespace Slic3r::GUI::JusPrin::V1Import
