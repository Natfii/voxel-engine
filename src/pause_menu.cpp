#include "pause_menu.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

PauseMenu::PauseMenu(GLFWwindow* window) : window(window) {}

bool PauseMenu::render() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(displaySize, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.5f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    ImGui::Begin("PauseMenu", nullptr, flags);

    float centerX = displaySize.x * 0.5f;
    float centerY = displaySize.y * 0.5f;
    ImGui::SetCursorPos(ImVec2(centerX - 50.0f, centerY - 20.0f));

    bool resumeClicked = false;
    if (ImGui::Button("Resume", ImVec2(100.0f, 0.0f))) {
        resumeClicked = true;
    }

    ImGui::SetCursorPos(ImVec2(centerX - 50.0f, centerY + 20.0f));
    if (ImGui::Button("Quit", ImVec2(100.0f, 0.0f))) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    ImGui::End();
    ImGui::PopStyleColor();

    return resumeClicked;
}