#pragma once

// What JusPrin tells its server each time it starts, and the install id it
// keeps for that. GUI-free; LaunchReport sends it.

#include <cstddef>
#include <string>

namespace Slic3r { class AppConfig; }

namespace Slic3r::GUI::JusPrin::Updates {

// A random id made the first time this install starts and kept in AppConfig.
// It names the install, not the person: nothing ties it to an account, and a
// fresh install makes a new one.
inline constexpr char        kInstallIdKey[]  = "jusprin_install_id";
inline constexpr std::size_t kInstallIdLength = 32; // lowercase hex digits

// The stored id, made and stored first when there is none or it is not an id.
std::string install_id(AppConfig& config);

// The body of the start report, as the server reads it: the install id, the
// app's version label and what the system calls itself.
std::string launch_report_body(const std::string& install_id, const std::string& app, const std::string& os);

} // namespace Slic3r::GUI::JusPrin::Updates
