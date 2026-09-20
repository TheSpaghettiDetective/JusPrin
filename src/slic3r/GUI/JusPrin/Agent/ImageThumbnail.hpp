#pragma once

// The one wx-dependent piece of attachment preview generation: decoding an
// arbitrary image and downscaling it for AgentHost::ImageThumbnailFn. Kept
// out of AgentHost.cpp (and off agent_bridge_tests' source list) because
// that target is deliberately built without wx; the real app links this
// file and wires image_thumbnail_data_url in after constructing its
// AgentHost, and the GUI-free tests wire a fake instead.

#include <string>

namespace Slic3r::GUI::JusPrin::Agent {

// A small JPEG thumbnail of `bytes`, downscaled to at most `long_edge`
// pixels on its longer side regardless of the original's size or format,
// returned as a data URL. Empty on anything wxImage cannot decode.
std::string image_thumbnail_data_url(const std::string& bytes, int long_edge = 256);

} // namespace Slic3r::GUI::JusPrin::Agent
