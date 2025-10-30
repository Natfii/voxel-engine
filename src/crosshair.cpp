#include "crosshair.h"
#include "imgui.h"

Crosshair::Crosshair()
    : visible(true), size(10.0f), thickness(2.0f), gap(3.0f) {}

void Crosshair::render() {
    if (!visible) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;

    float centerX = displaySize.x * 0.5f;
    float centerY = displaySize.y * 0.5f;

    // Create an invisible window that covers the entire screen
    // This allows us to draw on the screen without creating a visible UI window
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(displaySize, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0)); // Fully transparent window

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoInputs
                           | ImGuiWindowFlags_NoSavedSettings
                           | ImGuiWindowFlags_NoFocusOnAppearing
                           | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("Crosshair", nullptr, flags);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 color = IM_COL32(0, 0, 0, 191); // Black with 75% opacity (191/255 ≈ 0.75)

    // Draw horizontal line (left and right of center)
    drawList->AddLine(
        ImVec2(centerX - size - gap, centerY),
        ImVec2(centerX - gap, centerY),
        color,
        thickness
    );
    drawList->AddLine(
        ImVec2(centerX + gap, centerY),
        ImVec2(centerX + size + gap, centerY),
        color,
        thickness
    );

    // Draw vertical line (top and bottom of center)
    drawList->AddLine(
        ImVec2(centerX, centerY - size - gap),
        ImVec2(centerX, centerY - gap),
        color,
        thickness
    );
    drawList->AddLine(
        ImVec2(centerX, centerY + gap),
        ImVec2(centerX, centerY + size + gap),
        color,
        thickness
    );

    ImGui::End();
    ImGui::PopStyleColor();
}

void Crosshair::setVisible(bool visible) {
    this->visible = visible;
}

bool Crosshair::isVisible() const {
    return visible;
}
