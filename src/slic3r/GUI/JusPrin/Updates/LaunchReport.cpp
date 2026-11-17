#include "LaunchReport.hpp"

#include "LaunchRecord.hpp"
#include "Version.hpp"

#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentConfiguration.hpp"
#include "slic3r/Utils/Http.hpp"

#include <boost/log/trivial.hpp>

#include <wx/utils.h>

#include <string>

namespace Slic3r::GUI::JusPrin::Updates {

void report_launch()
{
    // Recreating the GUI, for a language change, attaches the shell again;
    // the run still started once.
    static bool reported = false;
    if (reported || wxGetApp().app_config == nullptr)
        return;
    reported = true;

    AppConfig& config = *wxGetApp().app_config;
    // The same stealth mode the update check reads: the person asked the app
    // to make no calls of its own.
    if (config.get_bool("stealth_mode"))
        return;

    const std::string body = launch_report_body(install_id(config), version_label(), std::string(wxGetOsDescription().ToUTF8()));
    Http::post(Agent::cloud_launches_url(&config))
        .header("Content-Type", "application/json")
        .header("User-Agent", user_agent())
        .set_post_body(body)
        .timeout_connect(5)
        .timeout_max(10)
        .on_error([](std::string, std::string error, unsigned status) {
            // A lost report is a count the server misses, nothing the person
            // needs to hear about.
            BOOST_LOG_TRIVIAL(info) << "JusPrin's start report was not delivered: HTTP " << status << ", " << error;
        })
        .perform();
}

} // namespace Slic3r::GUI::JusPrin::Updates
