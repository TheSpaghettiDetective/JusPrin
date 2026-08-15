#pragma once

class wxWindow;

namespace Slic3r::GUI {

class Plater;

namespace JusPrin {

wxWindow* create_full_window_ui_spike(wxWindow* parent, Plater& plater);

} // namespace JusPrin
} // namespace Slic3r::GUI
