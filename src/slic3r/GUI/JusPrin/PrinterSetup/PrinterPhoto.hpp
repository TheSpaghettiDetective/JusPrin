#pragma once

#include <cstddef>
#include <string>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

// Matches the Agent's per-request binary context cap (AgentHost.cpp,
// kAgentBinaryContextCap), so a photo the setup flow accepts is never larger
// than what the Agent itself would send to the same provider.
constexpr std::size_t kMaximumPhotoBytes = 10u * 1024u * 1024u;

struct PhotoCheck
{
    std::string mime;  // set when the bytes are an accepted image type
    std::string error; // user-facing reason otherwise
};

// Classifies by content, not by file name: PNG, JPEG, and WebP signatures are
// accepted; anything else, an empty file, or an oversized one is rejected.
PhotoCheck check_photo(const std::string& bytes);

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
