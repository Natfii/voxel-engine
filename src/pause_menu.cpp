#include "pause_menu.h"
#include "imgui.h"
#include <GLFW/glfw3.h>

PauseMenu::PauseMenu(GLFWwindow* window) : window(window) {}

PauseMenuAction PauseMenu::render() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(displaySize, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.5f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    ImGui::Begin("PauseMenu", nullptr, flags);

    float centerX = displaySize.x * 0.5f;
    float centerY = displaySize.y * 0.5f;
    float buttonWidth = 200.0f;
    float buttonHeight = 40.0f;
    float buttonSpacing = 15.0f;

    // Title
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetWindowFontScale(1.5f);
    ImGui::SetCursorPos(ImVec2(centerX - 40.0f, centerY - 120.0f));
    ImGui::Text("Paused");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    PauseMenuAction action = PauseMenuAction::NONE;

    // Resume button
    ImGui::SetCursorPos(ImVec2(centerX - buttonWidth * 0.5f, centerY - 30.0f));
    if (ImGui::Button("Resume", ImVec2(buttonWidth, buttonHeight))) {
        action = PauseMenuAction::RESUME;
    }

    // Exit to Main Menu button
    ImGui::SetCursorPos(ImVec2(centerX - buttonWidth * 0.5f, centerY - 30.0f + (buttonHeight + buttonSpacing)));
    if (ImGui::Button("Exit to Main Menu", ImVec2(buttonWidth, buttonHeight))) {
        action = PauseMenuAction::EXIT_TO_MENU;
    }

    // Quit button
    ImGui::SetCursorPos(ImVec2(centerX - buttonWidth * 0.5f, centerY - 30.0f + 2 * (buttonHeight + buttonSpacing)));
    if (ImGui::Button("Quit to Desktop", ImVec2(buttonWidth, buttonHeight))) {
        action = PauseMenuAction::QUIT;
    }

    ImGui::End();
    ImGui::PopStyleColor();

    return action;
}