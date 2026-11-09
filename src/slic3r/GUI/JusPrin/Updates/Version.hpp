#pragma once

#include "libslic3r_version.h"

#include <string>

namespace Slic3r::GUI::JusPrin::Updates {

// JusPrin's own version: what Preferences shows and what the update check
// compares with the version in each JusPrin release tag. It is separate
// from SoftFever_VERSION, OrcaSlicer's version, which this build keeps
// because 3MF projects, profile updates, the user_backup-v<version> folder
// and AppConfig's upgrade logic all read it.
inline constexpr char kVersion[] = "2.0.0";

// The version a problem report or a saved chat names: JusPrin's and the
// OrcaSlicer release it is built on, as "JusPrin 2.0.0 (OrcaSlicer 2.5.0)".
// Not SLIC3R_VERSION, which is OrcaSlicer's Bambu network-plugin
// compatibility number (02.08.01.55), not a release anyone can look up.
inline std::string version_label() { return std::string("JusPrin ") + kVersion + " (OrcaSlicer " + SoftFever_VERSION + ")"; }

// The User-Agent of every request JusPrin sends to its own server, so the
// server can tell JusPrin releases apart. Orca's Http sends
// "JusPrin/<SoftFever_VERSION>" by default, which names the OrcaSlicer
// release and is the same for every JusPrin release built on it.
inline std::string user_agent() { return std::string("JusPrin/") + kVersion + " OrcaSlicer/" + SoftFever_VERSION; }

} // namespace Slic3r::GUI::JusPrin::Updates
