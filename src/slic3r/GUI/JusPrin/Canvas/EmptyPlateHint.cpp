#include "EmptyPlateHint.hpp"

#include "slic3r/GUI/JusPrin/Brand/BrandPalette.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <imgui/imgui.h>

#include <cmath>

namespace Slic3r::GUI::JusPrin {

void render_empty_plate_hint(GLCanvas3D& canvas)
{
    // Without the token file the app keeps OrcaSlicer's stock presentation.
    const ShellTheme* theme = brand_theme();
    if (theme == nullptr)
        return;
    const std::string text   = _u8L("Drop a model here or add one");
    const Size        size   = canvas.get_canvas_size();
    const ImVec2      extent = ImGui::CalcTextSize(text.c_str());
    const wxColour&   colour = theme->palette(GUI_App::dark_mode()).text_secondary;
    ImGui::SetNextWindowPos(ImVec2(std::round((size.get_width() - extent.x) / 2), std::round((size.get_height() - extent.y) / 2)));
    ImGui::SetNextWindowSize(extent);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0.f, 0.f));
    ImGui::Begin("##jusprin_empty_plate_hint", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoInputs);
    ImGui::PopStyleVar(3);
    ImGui::GetWindowDrawList()->AddText(ImGui::GetWindowPos(),
                                        IM_COL32(colour.Red(), colour.Green(), colour.Blue(), colour.Alpha()), text.c_str());
    ImGui::End();
}

} // namespace Slic3r::GUI::JusPrin
