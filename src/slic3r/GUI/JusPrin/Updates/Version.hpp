#pragma once

namespace Slic3r::GUI::JusPrin::Updates {

// JusPrin's own version: what Preferences shows and what the update check
// compares with the version in each JusPrin release tag. It is separate
// from SoftFever_VERSION, OrcaSlicer's version, which this build keeps
// because 3MF projects, profile updates, the user_backup-v<version> folder
// and AppConfig's upgrade logic all read it.
inline constexpr char kVersion[] = "2.0.0";

} // namespace Slic3r::GUI::JusPrin::Updates
