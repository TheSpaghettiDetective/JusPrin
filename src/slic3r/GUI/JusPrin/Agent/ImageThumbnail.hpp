#pragma once

// The one wx-dependent piece of attachment preview generation: decoding an
// arbitrary image and downscaling it for AgentHost::ImageThumbnailFn. Kept
// out of AgentHost.cpp (and off agent_bridge_tests' source list) because
// that target is deliberately built without wx; the real app links this
// file and wires image_thumbnail_data_url in after constructing its
// AgentHost, and the GUI-free tests wire a fake instead.

#include <optional>
#include <string>

namespace Slic3r::GUI::JusPrin::Agent {

struct EncodedImage
{
    std::string mime;
    std::string bytes;
};

// An attached image as the model is sent it. Models only look at so many
// pixels: Claude scales anything over a 2576-pixel long edge down (its
// vision docs; older models stop at 1568), and the Claude API takes at most
// 10 MB of base64 per image, which a 10 MiB photo exceeds once encoded. A
// larger image is scaled to `long_edge` and re-encoded, as JPEG, or PNG when
// it has transparency; nothing when it is already small enough or cannot be
// decoded (the original is then sent as it is).
std::optional<EncodedImage> image_for_model(const std::string& bytes, int long_edge = 2576,
                                            std::size_t max_bytes = 2u * 1024u * 1024u);

// A small JPEG thumbnail of `bytes`, downscaled to at most `long_edge`
// pixels on its longer side regardless of the original's size or format,
// returned as a data URL. Empty on anything wxImage cannot decode.
std::string image_thumbnail_data_url(const std::string& bytes, int long_edge = 256);

} // namespace Slic3r::GUI::JusPrin::Agent
